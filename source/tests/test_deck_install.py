from paths import ROOT
import ctypes
import ctypes.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, str(ROOT / 'packaging/steamdeck'))
import deck_install as deck
import steam_shortcut as steam


def write(path, data=b'fixture'):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def sfo(path, title='CUSA04555', version='01.19'):
    fields = {'TITLE_ID': title, 'APP_VER': version}
    keys, values, entries = bytearray(), bytearray(), bytearray()
    for name, value in fields.items():
        data = value.encode() + b'\0'
        entries.extend(struct.pack('<HHIII', len(keys), 0x204, len(data), len(data), len(values)))
        keys.extend(name.encode() + b'\0')
        values.extend(data)
    key_offset = 20 + len(entries)
    write(path, struct.pack('<5I', 0x46535000, 0x101, key_offset, key_offset + len(keys), len(fields)) + entries + keys + values)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / 'extracted app'
        self.game = self.root / 'game/CUSA04555'
        self.modules = self.root / 'sys_modules'
        self.data = self.root / 'user-data'
        self.profile = {'name': 'Fortune', 'title_id': 'CUSA04555', 'version': '01.19', 'slug': 'doax3-fortune'}
        for name in ('doax3-launcher', 'bin/theorder-probe', 'bin/theorder-gpu-capabilities',
                     'Launch-DOAX3.sh', 'install.sh', 'share/doax3.png'):
            write(self.app / name)
        write(self.app / 'share/game.json', json.dumps(self.profile).encode())
        sfo(self.game / 'sce_sys/param.sfo')
        for name in ('eboot.bin', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx'):
            write(self.game / name)
        for name in ('libSceFiber.sprx', 'ulobjmgr.sprx', 'libSceUlt.sprx'):
            write(self.modules / name)
        self.steam = self.root / 'steam'
        self.shortcut = self.steam / 'userdata/1234/config/shortcuts.vdf'
        self.original = steam.dump_vdf({'shortcuts': {'0': {'appid': 1, 'AppName': 'Other Game', 'tags': {'0': 'keep'}}}})
        write(self.shortcut, self.original)

    def test_finds_sibling_game_modules_and_rejects_wrong_edition(self):
        self.assertEqual(deck.discover_inputs(self.app, self.profile), (self.game.resolve(), self.modules.resolve()))
        self.assertIsNone(deck.discover_inputs(self.app, dict(self.profile, title_id='CUSA14173', version='01.08'))[0])

    def test_native_registration_keeps_other_games_and_backs_up_vdf(self):
        with mock.patch.object(steam, 'steam_running', return_value=False):
            deck.add_to_steam(self.app, self.profile, self.steam)
        entries = steam.parse_vdf(self.shortcut.read_bytes())['shortcuts']
        self.assertEqual(entries['0']['AppName'], 'Other Game')
        self.assertEqual(entries['0']['tags'], {'0': 'keep'})
        self.assertEqual(entries['1']['AppName'], 'DOAX3 Fortune')
        self.assertIn('Launch-DOAX3.sh', entries['1']['Exe'])
        self.assertEqual(next(self.shortcut.parent.glob('*.backup-*')).read_bytes(), self.original)

    def test_two_editions_have_separate_entries_and_repeat_updates(self):
        with mock.patch.object(steam, 'steam_running', return_value=False):
            deck.add_to_steam(self.app, self.profile, self.steam)
            deck.add_to_steam(self.app, dict(self.profile, name='Scarlet'), self.steam)
            deck.add_to_steam(self.app, self.profile, self.steam)
        names = [value['AppName'] for value in steam.parse_vdf(self.shortcut.read_bytes())['shortcuts'].values()]
        self.assertCountEqual(names, ['Other Game', 'DOAX3 Fortune', 'DOAX3 Scarlet'])

    def test_running_steam_and_malformed_vdf_are_never_overwritten(self):
        with mock.patch.object(steam, 'steam_running', return_value=True), self.assertRaises(RuntimeError):
            deck.add_to_steam(self.app, self.profile, self.steam)
        self.assertEqual(self.shortcut.read_bytes(), self.original)
        self.shortcut.write_bytes(b'not a vdf')
        with mock.patch.object(steam, 'steam_running', return_value=False), self.assertRaises(RuntimeError):
            deck.add_to_steam(self.app, self.profile, self.steam)
        self.assertEqual(self.shortcut.read_bytes(), b'not a vdf')

    def test_install_copies_modules_preserves_saves_and_refuses_overwrite(self):
        save = self.data / 'doax3-fortune/user/savedata/keep'
        write(save, b'previous save')
        destination = self.root / 'Installed App With Spaces'
        deck.install(self.app, destination, self.game, self.modules, self.profile,
                     data_home=self.data, register=False, prepare=False)
        locations = json.loads((self.data / 'doax3-fortune/locations.json').read_text())
        self.assertEqual(locations['game'], str(self.game.resolve()))
        self.assertEqual(locations['modules'], str(destination / 'sys_modules'))
        self.assertTrue((destination / 'sys_modules/libSceUlt.sprx').is_file())
        self.assertFalse((destination / 'game').exists())
        with self.assertRaises(RuntimeError):
            deck.install(self.app, destination, self.game, self.modules, self.profile,
                         data_home=self.data, register=False, prepare=False)
        self.assertEqual(save.read_bytes(), b'previous save')

    def test_missing_module_is_rejected_before_install(self):
        (self.modules / 'libSceUlt.sprx').unlink()
        destination = self.root / 'missing-input-install'
        with self.assertRaises(RuntimeError):
            deck.install(self.app, destination, self.game, self.modules, self.profile,
                         data_home=self.data, register=False, prepare=False)
        self.assertFalse(destination.exists())

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Uses Linux desktop launch parser')
    def test_desktop_exec_relocates_with_spaces_dollar_and_quote(self):
        folder = self.root / "Package $value's space"
        folder.mkdir()
        desktop = folder / 'Install-DOAX3.desktop'
        desktop.write_text(deck.installer_desktop('Fortune'))
        (folder / 'install.sh').write_text('#!/bin/bash\nprintf "%s" "$1" > "$(dirname "$0")/called"\n')
        gio = ctypes.CDLL(ctypes.util.find_library('gio-2.0'))
        gio.g_desktop_app_info_new_from_filename.argtypes = [ctypes.c_char_p]
        gio.g_desktop_app_info_new_from_filename.restype = ctypes.c_void_p
        gio.g_app_info_launch.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        gio.g_app_info_launch.restype = ctypes.c_int
        app = gio.g_desktop_app_info_new_from_filename(str(desktop).encode())
        self.assertTrue(app, desktop.read_text())
        self.assertTrue(gio.g_app_info_launch(app, None, None, None))
        for _ in range(40):
            if (folder / 'called').is_file():
                break
            time.sleep(0.05)
        self.assertEqual((folder / 'called').read_text(), '--gui')


if __name__ == '__main__':
    unittest.main()

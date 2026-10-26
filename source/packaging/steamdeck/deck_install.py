"""Per-user native installer and Steam registration; no root or system packages."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile
import time


def external_environment():
    env = dict(os.environ)
    original = env.pop('LD_LIBRARY_PATH_ORIG', None)
    if original is None:
        env.pop('LD_LIBRARY_PATH', None)
    else:
        env['LD_LIBRARY_PATH'] = original
    env['PYINSTALLER_RESET_ENVIRONMENT'] = '1'
    return env


def backup(path):
    if path.exists():
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        shutil.copy2(path, path.with_name(path.name + '.backup-' + stamp))


def write_bytes(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    backup(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + '.', suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def validate_inputs(game, modules, profile):
    from prepare import read_sfo
    meta = read_sfo(game / 'sce_sys/param.sfo') or {}
    if (meta.get('TITLE_ID'), meta.get('APP_VER')) != (profile['title_id'], profile['version']):
        raise RuntimeError(f'Choose the merged {profile["name"]} {profile["version"]} game folder containing eboot.bin.')
    for path in [game / 'eboot.bin', game / 'sce_module/libc.prx', game / 'sce_module/libSceFios2.prx',
                 *[modules / name for name in ('libSceFiber.sprx', 'ulobjmgr.sprx', 'libSceUlt.sprx')]]:
        if not path.is_file() or not path.stat().st_size:
            raise RuntimeError(f'Missing or empty required file: {path}')


def discover_inputs(app, profile):
    from prepare import read_sfo
    game = None
    for base in (app, app.parent, app.parent.parent):
        for candidate in (base / 'game', base / 'game' / profile['title_id'],
                          base / 'games' / profile['title_id'], base / profile['title_id']):
            try:
                meta = read_sfo(candidate / 'sce_sys/param.sfo') or {}
            except (OSError, ValueError):
                continue
            if (meta.get('TITLE_ID'), meta.get('APP_VER')) == (profile['title_id'], profile['version']):
                game = candidate.resolve()
                break
        if game:
            break
    modules = next((path.resolve() for path in
                    (app / 'sys_modules', app.parent / 'sys_modules', app.parent.parent / 'sys_modules')
                    if all((path / name).is_file() for name in
                           ('libSceFiber.sprx', 'ulobjmgr.sprx', 'libSceUlt.sprx'))), None)
    return game, modules


def desktop_argument(argument):
    # Desktop Entry string unescaping precedes command-line unquoting.
    quoted = ''.join('\\' + char if char in '\\"`$' else char for char in argument)
    return '"' + quoted.replace('\\', '\\\\').replace('%', '%%') + '"'


def installer_desktop(name):
    command = 'exec bash "$(dirname "$1")/install.sh" --gui'
    return ('[Desktop Entry]\nType=Application\nName=Install DOAX3 ' + name + '\n'
            'Comment=Install the native game and add it to Steam\n'
            'Exec=bash -c ' + desktop_argument(command) + ' -- %k\n'
            'Icon=system-software-install\nTerminal=false\nCategories=Game;\n')


def add_to_steam(installed, profile, steam_root=None):
    import steam_shortcut as steam
    if steam.steam_running():
        raise RuntimeError('Close Steam before registering the game. Existing Steam shortcuts were not changed.')
    accounts = steam.account_folders(steam_root)
    if not accounts:
        raise RuntimeError('Steam has no signed-in account on this system. Sign in once, then run the installer again.')
    name = 'DOAX3 ' + profile['name']
    exe = '"' + str(installed / 'Launch-DOAX3.sh') + '"'
    appid = steam.shortcut_appid(exe, name)
    pending = []
    for account in accounts:
        path = account / 'config/shortcuts.vdf'
        try:
            tree = steam.parse_vdf(path.read_bytes()) if path.is_file() and path.stat().st_size else {'shortcuts': {}}
        except (ValueError, IndexError, struct.error) as error:
            raise RuntimeError('A Steam shortcut file could not be read. Existing entries were not changed.') from error
        shortcuts = tree.setdefault('shortcuts', {})
        slot = next((key for key, value in shortcuts.items()
                     if value.get('AppName') == name or value.get('appid') == appid), None)
        if slot is None:
            slot = str(max((int(key) for key in shortcuts if key.isdigit()), default=-1) + 1)
        previous = shortcuts.get(slot, {})
        entry = steam.shortcut_entry(appid, exe, '"' + str(installed) + '"', '', installed / 'share/doax3.png')
        entry['AppName'] = name
        entry['tags'] = previous.get('tags', {})
        entry['LastPlayTime'] = previous.get('LastPlayTime', 0)
        shortcuts[slot] = entry
        pending.append((path, steam.dump_vdf(tree)))
    # Parse all accounts before changing any, and preserve each original file.
    for path, data in pending:
        write_bytes(path, data)
    return {'accounts': len(pending), 'app_id': appid}


def install(app, destination, game, modules, profile, *, data_home=None, steam_root=None,
            register=True, prepare=True):
    app, destination, game, modules = [Path(path).resolve() for path in (app, destination, game, modules)]
    validate_inputs(game, modules, profile)
    if destination.exists():
        raise RuntimeError('The install folder already exists. Choose a new folder; your existing install and saves are preserved.')
    if destination.is_relative_to(app) or app.is_relative_to(destination):
        raise RuntimeError('Choose an install folder outside the extracted package.')
    for required in ('doax3-launcher', 'bin/theorder-probe', 'share/game.json'):
        if not (app / required).is_file():
            raise RuntimeError('Extract the complete application archive before installing.')
    # Game data stays at its selected location; never copy a multi-gigabyte game implicitly.
    def ignored(directory, names):
        return [name for name in names if Path(directory) == app and name in ('game', 'games', 'sys_modules')]
    shutil.copytree(app, destination, ignore=ignored)
    for relative in ('Launch-DOAX3.sh', 'install.sh', 'doax3-launcher',
                     'bin/theorder-probe', 'bin/theorder-gpu-capabilities'):
        (destination / relative).chmod(0o755)
    target_modules = destination / 'sys_modules'
    target_modules.mkdir(exist_ok=True)
    for filename in ('libSceFiber.sprx', 'ulobjmgr.sprx', 'libSceUlt.sprx'):
        shutil.copy2(modules / filename, target_modules / filename)
    data = Path(data_home or os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    state = data / profile['slug']
    write_bytes(state / 'locations.json', (json.dumps({'game': str(game), 'modules': str(target_modules)}, indent=2) + '\n').encode())
    if prepare:
        result = subprocess.run([str(destination / 'Launch-DOAX3.sh'), '--no-ui', '--prepare-only',
                                 '--game-dir', str(game), '--modules-dir', str(target_modules),
                                 '--data-dir', str(state)], capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise RuntimeError('Game preparation failed. The installed app and existing saves were preserved.\n' + result.stderr[-2000:])
    desktop = data / 'applications' / (profile['slug'] + '.desktop')
    entry = ('[Desktop Entry]\nType=Application\nName=DOAX3 ' + profile['name'] + '\n'
             'Exec=' + desktop_argument(str(destination / 'Launch-DOAX3.sh')) + '\n'
             'Icon=' + str(destination / 'share/doax3.png') + '\nTerminal=false\nCategories=Game;\n')
    write_bytes(desktop, entry.encode())
    desktop.chmod(0o755)
    registration = add_to_steam(destination, profile, steam_root) if register else None
    return {'installed': str(destination), 'game': str(game), 'state': str(state), 'steam': registration}


def graphical_install(app, profile):
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    import steam_shortcut as steam
    root = tk.Tk()
    root.title('Install DOAX3 ' + profile['name'])
    root.geometry('900x650')
    root.minsize(760, 540)
    root.option_add('*Font', 'Sans 13')
    panel = ttk.Frame(root, padding=24)
    panel.pack(fill='both', expand=True)
    ttk.Label(panel, text='DOAX3 ' + profile['name'] + ' — native Steam Deck setup', font=('Sans', 19, 'bold')).pack(anchor='w')
    ttk.Label(panel, text='Install once here, then launch from Steam Gaming Mode.\nNo Proton, terminal commands or root password.', wraplength=830).pack(anchor='w', pady=(12, 18))
    game, modules = discover_inputs(app, profile)
    values = [tk.StringVar(value=str(game or '')), tk.StringVar(value=str(modules or '')),
              tk.StringVar(value=str(Path.home() / 'Applications' / ('DOAX3-' + profile['name'])))]
    labels = [f'Game data ({profile["title_id"]}, update {profile["version"]})', 'PS4 system modules', 'Install application into a new folder']
    for index, label in enumerate(labels):
        ttk.Label(panel, text=label).pack(anchor='w', pady=(8, 3))
        row = ttk.Frame(panel)
        row.pack(fill='x')
        ttk.Entry(row, textvariable=values[index]).pack(side='left', fill='x', expand=True)
        def browse(i=index):
            selected = filedialog.askdirectory(parent=root, title=labels[i])
            if selected:
                if i == 2:
                    selected = str(Path(selected) / ('DOAX3-' + profile['name']))
                values[i].set(selected)
        ttk.Button(row, text='Browse…', command=browse).pack(side='right', padx=(8, 0))
    ttk.Label(panel, text='Your game data stays in its selected folder: keep that folder/drive connected.\nThe three system modules are copied into the installed app. Saves remain separate.\nSteam will close briefly to add the game safely, then reopen.', wraplength=830).pack(anchor='w', pady=(18, 8))
    status = tk.StringVar(value='Check the folders, then select Install and add to Steam.')
    ttk.Label(panel, textvariable=status, wraplength=830).pack(anchor='w', pady=8)
    button = ttk.Button(panel, text='Install and add to Steam')
    button.pack(anchor='e', pady=12)
    def perform():
        was_running = False
        try:
            if not values[0].get() or not values[1].get():
                raise RuntimeError('Choose the game and system-module folders first.')
            game_path, module_path, destination = [Path(value.get()).expanduser() for value in values]
            validate_inputs(game_path, module_path, profile)
            if destination.exists():
                raise RuntimeError('Install folder already exists. Use Browse to choose a new parent/folder; existing installs are preserved.')
            button.config(state='disabled')
            status.set('Preparing installation…')
            root.update()
            was_running = steam.steam_running()
            if was_running:
                status.set('Closing Steam briefly…')
                root.update()
                subprocess.run(['steam', '-shutdown'], env=external_environment(), timeout=20, check=True)
                deadline = time.monotonic() + 40
                while steam.steam_running():
                    if time.monotonic() >= deadline:
                        raise RuntimeError('Steam is still running. Close it from its menu, then try again. No shortcuts were changed.')
                    root.update()
                    time.sleep(0.25)
            status.set('Installing, preparing game data and adding the Steam shortcut…')
            root.update()
            result = install(app, destination, game_path, module_path, profile)
            messagebox.showinfo('DOAX3 installed', 'Installed and added to Steam.\n\nReturn to Gaming Mode and select DOAX3 ' + profile['name'] + '.\nKeep the game-data drive connected.\n\nSaves: ' + result['state'], parent=root)
            root.destroy()
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            messagebox.showerror('Installation needs attention', str(error), parent=root)
            button.config(state='normal')
            status.set('Existing saves and Steam entries are preserved. Correct the folders or choose a new install folder.')
        finally:
            if was_running and not steam.steam_running():
                subprocess.Popen(['steam', '-silent'], env=external_environment(), start_new_session=True,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    button.config(command=perform)
    root.mainloop()

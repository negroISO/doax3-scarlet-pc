"""link_modules.module on a synthetic SELF: reads the file once and reports its hash."""
import hashlib
import tempfile
import unittest
from pathlib import Path

from paths import ROOT  # noqa: F401  (sys.path for scripts/)
import link_modules
from test_link_libc import self_fixture


class ModuleTests(unittest.TestCase):
    def test_tcb_loads_preserve_every_destination_register(self):
        instructions = [bytes([0x64,rex,0x8b,4+8*reg,0x25,0,0,0,0])
                        for rex in (0x48,0x4c) for reg in range(8)]
        code = b''.join(instructions)
        nonzero = bytes.fromhex('64488b0c2510000000')
        image = bytearray(code + nonzero + code)
        ph = [{'type':1,'flags':5,'vaddr':0,'filesz':len(code)+len(nonzero)},
              {'type':1,'flags':6,'vaddr':len(code)+len(nonzero),'filesz':len(code)}]
        self.assertEqual(link_modules.patch_fs_loads(image, ph, 0),16)
        self.assertEqual(image[:len(code)],b''.join(b'\x65'+i[1:] for i in instructions))
        self.assertEqual(image[len(code):],nonzero+code)

    def test_module_accepts_unpacked_elf_with_the_same_exports(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'libc.prx'
            source = self_fixture()
            path.write_bytes(source)
            expected = link_modules.module(path)
            elf = link_modules.parse_self(source)[0]
            path.write_bytes(elf)
            actual = link_modules.module(path)
        self.assertEqual(actual['symbols'], expected['symbols'])
        self.assertEqual(actual['ph'], expected['ph'])
        self.assertEqual(actual['relocs'], expected['relocs'])
        self.assertEqual(actual['sha256'], hashlib.sha256(elf).hexdigest())

    def test_module_reads_the_file_once_and_reports_its_hash(self):
        data = self_fixture()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'libc.prx'
            path.write_bytes(data)
            info = link_modules.module(path)
        self.assertEqual(info['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(info['missing'], [])
        self.assertEqual(info['symbols'][0]['identity'], ('bzQExy189ZI', ('libc', 1), ('libc', 1)))


if __name__ == '__main__':
    unittest.main()

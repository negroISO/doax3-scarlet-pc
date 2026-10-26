import struct
import unittest

import paths  # noqa: F401  (puts scripts/ on sys.path)
import eboot_pe


def read_pe(data):
    """Image base, {rva: section bytes}, imports {address: (dll, name)}, exports {name: address}."""
    pe, = struct.unpack_from('<I', data, 0x3c)
    assert data[pe:pe + 6] == b'PE\0\0\x64\x86'
    count, = struct.unpack_from('<H', data, pe + 6)
    optional_size, = struct.unpack_from('<H', data, pe + 20)
    optional = pe + 24
    base, = struct.unpack_from('<Q', data, optional + 24)
    directories = struct.unpack_from('<4I', data, optional + 112)
    sections = [struct.unpack_from('<IIII', data, optional + optional_size + i * 40 + 8) for i in range(count)]

    def at(rva, size):
        size_, va, raw_size, raw = next(s for s in sections if s[1] <= rva < s[1] + s[0])
        return data[raw + rva - va:raw + rva - va + size]

    def string(rva):
        return at(rva, 256).split(b'\0')[0].decode()
    imports, rva = {}, directories[2]
    while any(descriptor := struct.unpack('<5I', at(rva, 20))):
        lookup, _, _, dll, first = descriptor
        for i in range(1000):
            entry, = struct.unpack('<Q', at(lookup + i * 8, 8))
            if not entry:
                break
            assert struct.unpack('<Q', at(first + i * 8, 8))[0] == entry
            imports[base + first + i * 8] = (string(dll), string(entry + 2))
        rva += 20
    *_, names, functions_rva, names_rva, ordinals_rva = struct.unpack('<IIHHIIIIIII', at(directories[0], 40))
    exports = {}
    for i in range(names):
        name, = struct.unpack('<I', at(names_rva + i * 4, 4))
        index, = struct.unpack('<H', at(ordinals_rva + i * 2, 2))
        exports[string(name)] = base + struct.unpack('<I', at(functions_rva + index * 4, 4))[0]
    return base, at, imports, exports


class EbootPeTest(unittest.TestCase):
    def test_round_trip(self):
        image = bytearray(range(256)) * 48  # 0x3000 bytes: code at 0, data at 0x2000
        slots = {0x2000: ('libc', 'malloc'), 0x2008: ('libc', 'free'),
                 0x2010: ('libkernel', 'sceKernelUsleep'), 0x2100: ('libc', 'malloc')}
        data = eboot_pe.pe_image(image, [(0, 0x1000, 5), (0x2000, 0x1000, 6)], 0xa0, slots,
                                 {0x10: 'sub_10', 0xa0: '_start'})
        base, at, imports, exports = read_pe(data)
        to_pe = eboot_pe.BASE
        self.assertEqual(base + eboot_pe.HEADERS, to_pe)  # eboot address A is at BASE + A
        self.assertEqual(at(eboot_pe.HEADERS + 0x10, 16), image[0x10:0x20])
        self.assertEqual(imports, {to_pe + 0x2000: ('libc.prx', 'malloc'), to_pe + 0x2008: ('libc.prx', 'free'),
                                   to_pe + 0x2010: ('libkernel.prx', 'sceKernelUsleep'),
                                   to_pe + 0x2100: ('libc.prx', 'malloc')})
        self.assertEqual(exports, {'sub_10': to_pe + 0x10, '_start': to_pe + 0xa0})
        self.assertEqual(at(eboot_pe.HEADERS + 0x2018, 8), image[0x2018:0x2020])  # data between slots kept


if __name__ == '__main__':
    unittest.main()

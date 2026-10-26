"""Wrap the eboot in an x86-64 PE so REA's Ghidra provider can open it on Windows.

REA admits only PE images to Ghidra on Windows. The PE maps eboot address A at BASE + A (the
PE headers need room below eboot address 0) with every relocation applied; each import slot
becomes an IAT entry named from src/import_names.inc, and each .eh_frame_hdr function start is
exported as sub_<A>, A in eboot coordinates as in docs/ENGINE_RE.md. For static analysis only:
the image does not run. Import it with the System V convention (Ghidra x86:LE:64:default, gcc).
Output: out/eboot_ghidra.exe.
"""
import argparse
import re
import struct
from pathlib import Path

from link_modules import canonical, module
from prepare import span, unpack

ROOT = Path(__file__).resolve().parent.parent
BASE = 0x100000000  # PE address of eboot address 0
HEADERS = 0x10000  # its RVA
KNOWN = {0x20: '_init', 0xa0: '_start', 0x2981b0: 'main'}  # docs/ENGINE_RE.md


def align(value, to):
    return -(-value // to) * to


def function_starts(image, hdr):
    """Initial locations of the .eh_frame_hdr search table at eboot address hdr."""
    if bytes(image[hdr:hdr + 4]) != b'\x01\x1b\x03\x3b':  # version 1, pcrel, udata4, datarel sdata4
        raise ValueError(f'unexpected .eh_frame_hdr encodings: {image[hdr:hdr + 4].hex()}')
    count, = unpack('<I', image, hdr + 8)
    return {hdr + unpack('<i', image, hdr + 12 + i * 8)[0] for i in range(count)}


def load(game):
    """The relocated eboot image, its (vaddr, memsz, flags) segments, entry, import slots
    {address: (library, name)} and function starts {address: name}."""
    m = module(game / 'eboot.bin')
    loads = [p for p in m['ph'] if p['type'] in (1, 0x61000010)]
    image = bytearray(max(p['vaddr'] + p['memsz'] for p in loads))
    for p in loads:
        image[p['vaddr']:p['vaddr'] + p['filesz']] = span(m['elf'], p['offset'], p['filesz'])
    names = dict(re.findall(r'\{"([^"]+)","([^"]+)"\}', (ROOT / 'src/import_names.inc').read_text()))
    slots = {}
    for target, kind, index, addend in m['relocs']:
        symbol = m['symbols'][index]
        if kind == 8:
            struct.pack_into('<Q', image, target, BASE + addend)
        elif kind not in (1, 6, 7):
            raise ValueError(f'unsupported relocation {kind} at {target:#x}')
        elif symbol['section'] == 0xfff1:
            struct.pack_into('<Q', image, target, symbol['value'] + addend)
        elif symbol['section']:
            struct.pack_into('<Q', image, target, BASE + symbol['value'] + addend)
        elif addend:
            raise ValueError(f'import with addend {addend} at {target:#x}: an IAT entry has none')
        else:
            key = canonical(symbol['identity'], symbol['name'])
            slots[target] = (symbol['identity'][1][0] if symbol['identity'] else 'unknown', names.get(key, key))
    hdr = next(p for p in m['ph'] if p['type'] == 0x6474e550)['vaddr']
    exports = {a: f'sub_{a:x}' for a in function_starts(image, hdr)} | KNOWN
    segments = [(p['vaddr'], p['memsz'], p['flags']) for p in loads]
    return image, segments, m['header'][4], slots, exports


def put(blob, data, rva, at):
    """Append data at a multiple of at; return its RVA (the blob starts at rva)."""
    blob.extend(bytes(-len(blob) % at))
    blob.extend(data)
    return rva + len(blob) - len(data)


def pe_image(image, segments, entry, slots, exports):
    """PE32+ bytes: one section per segment (memsz bytes: relocations also reach .bss),
    then .idata and .edata."""
    image = bytearray(image)
    idata_rva = align(HEADERS + max(v + size for v, size, _ in segments), 0x1000)
    runs = []  # adjacent slots of one library share an import descriptor
    for slot in sorted(slots):
        if runs and runs[-1][-1] + 8 == slot and slots[runs[-1][-1]][0] == slots[slot][0]:
            runs[-1].append(slot)
        else:
            runs.append([slot])
    idata = bytearray(20 * (len(runs) + 1))
    dlls, hints = {}, {}
    for i, run in enumerate(runs):
        library = slots[run[0]][0]
        if library not in dlls:
            dlls[library] = put(idata, f'{library}.prx\0'.encode(), idata_rva, 1)
        for slot in run:
            name = slots[slot][1]
            if name not in hints:
                hints[name] = put(idata, b'\0\0' + name.encode() + b'\0', idata_rva, 2)
            struct.pack_into('<Q', image, slot, hints[name])  # an unbound IAT repeats the lookup table
        lookup = put(idata, struct.pack(f'<{len(run) + 1}Q', *(hints[slots[s][1]] for s in run), 0),
                     idata_rva, 8)
        struct.pack_into('<5I', idata, i * 20, lookup, 0, 0, dlls[library], HEADERS + run[0])

    edata_rva = align(idata_rva + len(idata), 0x1000)
    addresses = sorted(exports)
    by_name = sorted(addresses, key=lambda a: exports[a].encode())
    count = len(addresses)
    edata = bytearray(40)
    functions = put(edata, struct.pack(f'<{count}I', *(HEADERS + a for a in addresses)), edata_rva, 4)
    strings = [put(edata, exports[a].encode() + b'\0', edata_rva, 1) for a in by_name]
    names = put(edata, struct.pack(f'<{count}I', *strings), edata_rva, 4)
    ordinal = {a: i for i, a in enumerate(addresses)}
    ordinals = put(edata, struct.pack(f'<{count}H', *(ordinal[a] for a in by_name)), edata_rva, 2)
    struct.pack_into('<IIHHIIIIIII', edata, 0, 0, 0, 0, 0, put(edata, b'eboot.bin\0', edata_rva, 1),
                     1, count, count, functions, names, ordinals)

    sections = []  # name, rva, bytes, characteristics
    for vaddr, size, flags in segments:  # PF_X 1, PF_W 2
        name, characteristics = ((b'.text', 0x60000020) if flags & 1 else (b'.data', 0xc0000040) if flags & 2
                                 else (b'.rdata', 0x40000040))
        sections.append((name, HEADERS + vaddr, image[vaddr:vaddr + size], characteristics))
    sections += [(b'.idata', idata_rva, idata, 0xc0000040), (b'.edata', edata_rva, edata, 0x40000040)]
    header_size, table, raw = 0x400, bytearray(), bytearray()
    for name, rva, data, characteristics in sections:
        size = align(len(data), 0x200)
        table += struct.pack('<8sIIIIIIHHI', name, len(data), rva, size, header_size + len(raw), 0, 0, 0, 0,
                             characteristics)
        raw += data + bytes(size - len(data))
    code = sum(align(len(s[2]), 0x200) for s in sections if s[3] & 0x20)
    directories = [edata_rva, len(edata), idata_rva, 20 * (len(runs) + 1)] + [0] * 28
    optional = struct.pack('<HBBIIIIIQIIHHHHHHIIIIHHQQQQII32I', 0x20b, 14, 0, code, len(raw) - code, 0,
                           HEADERS + entry, HEADERS, BASE - HEADERS, 0x1000, 0x200, 6, 0, 0, 0, 6, 0, 0,
                           align(edata_rva + len(edata), 0x1000), header_size, 0, 3, 0x100,
                           0x100000, 0x1000, 0x100000, 0x1000, 0, 16, *directories)
    # Machine x86-64; relocations stripped, executable, large-address aware.
    coff = struct.pack('<4sHHIIIHH', b'PE\0\0', 0x8664, len(sections), 0, 0, 0, len(optional), 0x23)
    dos = bytearray(64)
    dos[:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3c, len(dos))
    headers = dos + coff + optional + table
    if len(headers) > header_size:
        raise ValueError('PE headers overflow')
    return bytes(headers.ljust(header_size, b'\0') + raw)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'out/eboot_ghidra.exe')
    args = parser.parse_args()
    try:
        image, segments, entry, slots, exports = load(args.game)
        args.out.write_bytes(pe_image(image, segments, entry, slots, exports))
    except (ValueError, OSError, StopIteration, KeyError, IndexError) as error:
        parser.exit(1, f'eboot_pe failed: {error}\n')
    print(f'{args.out}: {len(exports)} functions, {len(slots)} import slots; '
          f'eboot address A is at {BASE:#x} + A')

"""Audit staged product files without loading or executing them."""
import argparse
import hashlib
import json
import platform
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE' + bytes(2):
        raise ValueError(f'Invalid PE: {path.name}')
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    if magic != 0x10B:
        raise ValueError(f'Expected PE32: {path.name}')
    sections = []
    for index in range(count):
        pos = optional + optional_size + index * 40
        virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', data, pos + 8)
        sections.append((address, max(virtual_size, raw_size), raw))

    def offset(rva):
        for address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        if rva < struct.unpack_from('<I', data, optional + 60)[0]:
            return rva
        raise ValueError(f'Unmapped RVA {rva:X} in {path.name}')

    imports = []
    import_rva = struct.unpack_from('<I', data, optional + 104)[0]
    if import_rva:
        pos = offset(import_rva)
        while any(struct.unpack_from('<IIIII', data, pos)):
            name_rva = struct.unpack_from('<I', data, pos + 12)[0]
            name_pos = offset(name_rva)
            imports.append(data[name_pos:data.index(0, name_pos)].decode('ascii'))
            pos += 20
    version = struct.unpack_from('<HH', data, optional + 48)
    forbidden = [name for name in imports if any(token in name.lower()
                 for token in ('msvcrt', 'msvcp', 'vcruntime', 'ucrtbase', 'api-ms-win-crt'))]
    return dict(name=path.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                machine=f'0x{machine:04X}', subsystem_version=f'{version[0]}.{version[1]:02}',
                imports=imports, dynamic_crt=forbidden,
                passed=machine == 0x14C and version <= (6, 1) and not forbidden)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('runtime', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    names = ['StoutWall.exe', 'assets.dll', 'gallery.dll', '7za.exe', 'cabarc.exe']
    records = [inspect(args.runtime / name) for name in names]
    obsolete = [name for name in ('network.dll', 'platform.dll', 'library.dll', '7z.dll', 'cabinet.dll')
                if (args.runtime / name).exists()]
    exe = records[0]
    self_dlls = [name for name in exe['imports'] if name.lower() in
                 ('network.dll', 'platform.dll', 'library.dll')]
    result = dict(system=platform.system(), system_version=platform.version(),
                  runtime=str(args.runtime.resolve()), files=records,
                  obsolete_modules=obsolete, self_code_dll_imports=self_dlls,
                  passed=all(record['passed'] for record in records) and not obsolete and not self_dlls)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()

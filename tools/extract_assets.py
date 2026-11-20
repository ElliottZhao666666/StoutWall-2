#!/usr/bin/env python3
"""Extract byte-exact assets and emit x86 resource-only DLL inputs."""
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from xml.etree import ElementTree

PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT.parent
PNG = b"\x89PNG\r\n\x1a\n"
ICO = b"\0\0\1\0"
JPEG = b"\xff\xd8\xff"


class AssetError(Exception):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def png_size(data, start):
    if data[start:start + 8] != PNG:
        return None
    pos, first, seen, ended, idat = start + 8, True, False, False, bytearray()
    width = height = depth = color = interlace = None
    while pos + 12 <= len(data):
        size = struct.unpack_from(">I", data, pos)[0]
        end = pos + 12 + size
        if end > len(data):
            return None
        kind, payload = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + size]
        crc = struct.unpack_from(">I", data, pos + 8 + size)[0]
        if zlib.crc32(kind + payload) & 0xffffffff != crc:
            return None
        if first:
            if kind != b"IHDR" or size != 13:
                return None
            width, height, depth, color, comp, filt, interlace = struct.unpack(">IIBBBBB", payload)
            depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
            if not width or not height or color not in depths or depth not in depths[color] or comp or filt or interlace not in (0, 1):
                return None
            first = False
        elif kind == b"IDAT":
            if ended:
                return None
            seen = True
            idat.extend(payload)
        else:
            if kind == b"IEND":
                if size or not seen:
                    return None
                image_end = end
                break
            if seen:
                ended = True
            if kind == b"IHDR":
                return None
        pos = end
    else:
        return None
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    passes = [(0, 0, 1, 1)] if interlace == 0 else [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    raw_size = 0
    for x, y, dx, dy in passes:
        w, h = max(0, (width - x + dx - 1) // dx), max(0, (height - y + dy - 1) // dy)
        if w and h:
            raw_size += h * (1 + (w * channels * depth + 7) // 8)
    if raw_size > 512 * 1024 * 1024:
        return None
    decoder = zlib.decompressobj()
    try:
        decoded = decoder.decompress(bytes(idat), raw_size + 1)
    except zlib.error:
        return None
    if len(decoded) != raw_size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        return None
    return image_end - start


def jpeg_size(data, start):
    if data[start:start + 3] != JPEG:
        return None
    pos, frame, scan = start + 2, False, False
    while pos < len(data):
        if data[pos] != 0xff:
            return None
        while pos < len(data) and data[pos] == 0xff:
            pos += 1
        if pos == len(data):
            return None
        marker, pos = data[pos], pos + 1
        if marker == 0xd9:
            return pos - start if frame and scan else None
        if marker in (0xd8, 0x00):
            return None
        if marker == 0x01 or 0xd0 <= marker <= 0xd7:
            continue
        if pos + 2 > len(data):
            return None
        size = struct.unpack_from(">H", data, pos)[0]
        if size < 2 or pos + size > len(data):
            return None
        if marker in (0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf):
            if size < 8:
                return None
            precision = data[pos + 2]
            h, w = struct.unpack_from(">HH", data, pos + 3)
            n = data[pos + 7]
            if not precision or not w or not h or not n or size != 8 + 3 * n:
                return None
            frame = True
        if marker == 0xda:
            n = data[pos + 2] if size >= 3 else 0
            if not frame or not n or size != 6 + 2 * n:
                return None
            scan = True
            pos += size
            while pos < len(data):
                hit = data.find(b"\xff", pos)
                if hit < 0 or hit + 1 >= len(data):
                    return None
                code = data[hit + 1]
                if code == 0 or 0xd0 <= code <= 0xd7:
                    pos = hit + 2
                    continue
                pos = hit
                break
            continue
        pos += size
    return None


def dib_ok(data, start, end, doubled=False):
    if start + 4 > end:
        return False
    hs = struct.unpack_from("<I", data, start)[0]
    if hs == 12:
        if start + 12 > end:
            return False
        w, h, planes, bits = struct.unpack_from("<HHHH", data, start + 4)
        comp, colors, entry = 0, 0, 3
    elif hs in (40, 52, 56, 108, 124) and start + hs <= end:
        w, h, planes, bits = struct.unpack_from("<iiHH", data, start + 4)
        comp = struct.unpack_from("<I", data, start + 16)[0]
        colors, entry = (struct.unpack_from("<I", data, start + 32)[0] if hs >= 40 else 0), 4
    else:
        return False
    if w <= 0 or not h or planes != 1 or bits not in (1, 2, 4, 8, 16, 24, 32) or comp not in (0, 1, 2, 3, 4, 5, 6):
        return False
    ah = abs(h)
    if doubled and ah % 2:
        return False
    ph = ah // 2 if doubled else ah
    if not ph:
        return False
    pixel = start + hs + (colors or (1 << bits if bits <= 8 else 0)) * entry
    if pixel > end:
        return False
    xor_bytes = (w * bits + 31) // 32 * 4 * ph
    mask_bytes = (w + 31) // 32 * 4 * ph if doubled else 0
    return pixel < end if comp in (1, 2, 4, 5) else pixel + xor_bytes + mask_bytes <= end


def ico_size(data, start):
    if data[start:start + 4] != ICO or start + 6 > len(data):
        return None
    reserved, kind, count = struct.unpack_from("<HHH", data, start)
    if reserved or kind != 1 or not count:
        return None
    dir_end = start + 6 + count * 16
    if dir_end > len(data):
        return None
    spans = []
    for i in range(count):
        ent = start + 6 + i * 16
        size, off = struct.unpack_from("<II", data, ent + 8)
        lo, hi = start + off, start + off + size
        if size < 8 or off < 6 + count * 16 or hi > len(data):
            return None
        part = data[lo:hi]
        if part.startswith(PNG):
            valid = png_size(part, 0) == len(part)
        elif part.startswith(b"BM"):
            valid = len(part) >= 14 and struct.unpack_from("<I", part, 2)[0] == len(part) and dib_ok(part, struct.unpack_from("<I", part, 10)[0], len(part), True)
        else:
            valid = dib_ok(part, 0, len(part), True)
        if not valid:
            return None
        spans.append((lo, hi))
    ordered = sorted(set(spans))
    for a, b in zip(ordered, ordered[1:]):
        if b[0] < a[1] and a != b:
            return None
    return max(hi for _, hi in spans) - start


def bmp_size(data, start):
    if data[start:start + 2] != b"BM" or start + 14 > len(data):
        return None
    size, offset = struct.unpack_from("<I", data, start + 2)[0], struct.unpack_from("<I", data, start + 10)[0]
    end = start + size
    if size < 26 or end > len(data) or offset < 14 or offset > size or not dib_ok(data, start + 14, end):
        return None
    return size


def scan_images(blob):
    found = []
    for pos in range(len(blob)):
        for magic, kind, parser in ((PNG, "PNG", png_size), (JPEG, "JPEG", jpeg_size), (ICO, "ICO", ico_size), (b"BM", "BMP", bmp_size)):
            if blob.startswith(magic, pos):
                size = parser(blob, pos)
                if size:
                    found.append((pos, size, kind))
                break
    return found


def segment(text):
    if not text or text in (".", "..") or any(c in text for c in "/\\\r\n\0\""):
        raise AssetError(f"unsafe resource-name segment: {text!r}")
    return text


def quoted(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        stream.write(data)
        tmp = Path(stream.name)
    os.replace(tmp, path)


class Catalog:
    def __init__(self, resources):
        self.resources, self.items, self.names = resources, [], set()

    def add(self, name, data, source, source_hash, fmt, offset=None):
        if name in self.names:
            raise AssetError(f"duplicate resource name: {name}")
        self.names.add(name)
        h = digest(data)
        rel = f"payload/{h}.bin"
        path = self.resources / rel
        if path.exists() and digest(path.read_bytes()) != h:
            raise AssetError(f"existing payload failed SHA-256 verification: {path}")
        if not path.exists():
            write_atomic(path, data)
        item = {"name": name, "type": "RCDATA", "format": fmt, "size": len(data),
                "sha256": h, "payload": rel, "source": source, "sourceSha256": source_hash}
        if offset is not None:
            item["sourceOffset"] = offset
        self.items.append(item)


def locate(explicit, leaf, globs):
    if explicit:
        return explicit
    found = shutil.which(leaf)
    if found:
        return found
    bases = [Path(r"C:\Program Files (x86)\Windows Kits\10\bin"),
             Path(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC")]
    for base, pattern in zip(bases, globs):
        if base.exists():
            hits = sorted(base.glob(pattern), reverse=True)
            if hits:
                return str(hits[0])
    raise AssetError(f"cannot find {leaf}; pass its path explicitly")


def build(resources, rc_arg, link_arg):
    rc = locate(rc_arg, "rc.exe", "*/x86/rc.exe")
    link = locate(link_arg, "link.exe", "*/bin/HostX64/x86/link.exe")
    subprocess.run([rc, "/nologo", "/c65001", "/foassets.res", "assets.rc"], cwd=resources, check=True)
    subprocess.run([rc, "/nologo", "/foapp.res", "app.rc"], cwd=resources, check=True)
    subprocess.run([link, "/nologo", "/dll", "/noentry", "/nodefaultlib", "/noimplib", "/machine:X86",
                    "/Brepro", "/out:assets.dll", "assets.res"], cwd=resources, check=True)


def extract(source, project, runtime, do_build, rc_arg, link_arg):
    source, project, runtime = source.resolve(), project.resolve(), runtime.resolve()
    resources = project / "resources"
    resources.mkdir(parents=True, exist_ok=True)
    cat = Catalog(resources)
    indices = {}
    for group in ("image", "audio"):
        index_path = source / group / "list.json"
        index = json.loads(index_path.read_text(encoding="utf-8-sig"))
        indices[group] = index
        for entry in index.get("items", []):
            logical = segment(str(entry["logicalName"]))
            path = (source / entry["relativePath"]).resolve()
            if not path.is_relative_to(source) or not path.is_file():
                raise AssetError(f"indexed asset missing or outside source: {path}")
            data = path.read_bytes()
            fmt = "ICO" if ico_size(data, 0) == len(data) else "opaque"
            prefix = "IMG" if group == "image" else "AUDIO"
            cat.add(f"{prefix}/{logical}", data, path.relative_to(source).as_posix(), digest(data), fmt)
            if group == "image" and logical == "icon":
                if fmt != "ICO":
                    raise AssetError(f"image/icon is not a structurally valid ICO: {path}")
                write_atomic(resources / "app.ico", data)
    icon = next((x for x in cat.items if x["name"] == "IMG/icon"), None)
    if icon is None:
        raise AssetError("image/list.json must contain logicalName=icon for the EXE")

    xmls = sorted((source / "src").glob("*.xml"), key=lambda p: p.name.casefold())
    xml_records = []
    for path in xmls:
        raw = path.read_bytes()
        source_hash = digest(raw)
        root = ElementTree.fromstring(raw)
        window = segment(root.get("名称") or path.stem)
        xml_records.append({"path": f"src/{path.name}", "size": len(raw), "sha256": source_hash})
        cat.add(f"XML/{window}/@source/xml", raw, f"src/{path.name}", source_hash, "XML")
        for el in root.iter():
            control = segment(el.get("名称") or el.tag)
            for prop, encoded in el.attrib.items():
                if not encoded:
                    continue
                try:
                    blob = base64.b64decode(encoded, validate=True)
                except (binascii.Error, ValueError):
                    continue
                if not blob:
                    continue
                prop = segment(prop)
                source_ref = f"src/{path.name}#{control}/@{prop}"
                cat.add(f"XML/{window}/{control}/{prop}/blob", blob, source_ref, source_hash,
                        "EXUI-extension" if prop == "扩展属性数据" else "base64-property")
                for ordinal, (offset, length, fmt) in enumerate(scan_images(blob)):
                    cat.add(f"XML/{window}/{control}/{prop}/{ordinal}", blob[offset:offset + length],
                            source_ref, source_hash, fmt, offset)

    rc_lines = ["#pragma code_page(65001)"]
    for item in cat.items:
        rc_lines.append(f"{quoted(item['name'])} RCDATA {quoted(item['payload'])}")
    write_atomic(resources / "assets.rc", ("\r\n".join(rc_lines) + "\r\n").encode("utf-8-sig"))
    version_resource = (
        "#pragma code_page(65001)\r\n"
        "#include <winver.h>\r\n"
        "101 ICON \"app.ico\"\r\n"
        "VS_VERSION_INFO VERSIONINFO\r\n"
        " FILEVERSION 1,1,0,2509\r\n"
        " PRODUCTVERSION 1,1,0,2509\r\n"
        " FILEFLAGSMASK 0x3fL\r\n"
        " FILEFLAGS 0\r\n"
        " FILEOS VOS_NT_WINDOWS32\r\n"
        " FILETYPE VFT_APP\r\n"
        "BEGIN\r\n"
        "  BLOCK \"StringFileInfo\"\r\n"
        "  BEGIN\r\n"
        "    BLOCK \"080404B0\"\r\n"
        "    BEGIN\r\n"
        "      VALUE \"FileDescription\", \"胖哥每日壁纸\"\r\n"
        "      VALUE \"FileVersion\", \"V1.1.0.2509\"\r\n"
        "      VALUE \"ProductName\", \"胖哥每日壁纸\"\r\n"
        "      VALUE \"ProductVersion\", \"V1.1.0.2509\"\r\n"
        "    END\r\n"
        "  END\r\n"
        "  BLOCK \"VarFileInfo\"\r\n"
        "  BEGIN\r\n"
        "    VALUE \"Translation\", 0x804, 1200\r\n"
        "  END\r\n"
        "END\r\n"
    )
    write_atomic(resources / "app.rc", version_resource.encode("utf-8-sig"))

    runtime_files = []
    if runtime.is_dir():
        for path in sorted(runtime.iterdir(), key=lambda p: p.name.casefold()):
            if path.is_file():
                ext = path.suffix.lower()
                category = "external-program" if ext == ".exe" else "external-library" if ext == ".dll" else "external-data-package" if ext in (".edt", ".cfg") else "external-file"
                runtime_files.append({"name": path.name, "size": path.stat().st_size,
                                      "sha256": digest(path.read_bytes()), "classification": category,
                                      "includedInAssetsDll": False})
    result = {"schemaVersion": 1, "sourceRoot": str(source), "resourceType": "RCDATA",
              "appIcon": {"source": "image/icon.bin", "payload": "app.ico", "size": icon["size"],
                          "sha256": icon["sha256"], "alsoIncludedAs": icon["name"]},
              "sourceIndexes": {"imageItems": len(indices["image"].get("items", [])),
                                "audioItems": len(indices["audio"].get("items", []))},
              "sourceXmlFiles": xml_records, "runtimeCopy": runtime_files,
              "excludedRuntimePrograms": [x["name"] for x in runtime_files if x["classification"] == "external-program"],
              "assetCount": len(cat.items), "totalPayloadBytes": sum(x["size"] for x in cat.items),
              "build": {"requested": do_build, "built": do_build or (resources / "assets.dll").is_file(),
                        "machine": "x86", "crt": "none; /NOENTRY, no object files"},
              "assets": cat.items}
    write_atomic(resources / "manifest.json", (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    if do_build:
        build(resources, rc_arg, link_arg)
        result["build"]["built"] = True
        write_atomic(resources / "manifest.json", (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=SOURCE)
    parser.add_argument("--project-root", type=Path, default=PROJECT)
    parser.add_argument("--runtime-copy", type=Path, default=SOURCE / "StoutWall_Rebuild" / "runtime-copy")
    parser.add_argument("--build", action="store_true", help="build assets.dll and app.res with Windows tools")
    parser.add_argument("--rc", help="path to Windows SDK rc.exe")
    parser.add_argument("--link", help="path to MSVC x86 link.exe")
    args = parser.parse_args()
    try:
        result = extract(args.source_root, args.project_root, args.runtime_copy, args.build, args.rc, args.link)
    except (AssetError, OSError, ValueError, json.JSONDecodeError, ElementTree.ParseError,
            subprocess.CalledProcessError) as exc:
        print(f"extract_assets: error: {exc}", file=sys.stderr)
        return 2
    print(f"assets={result['assetCount']} bytes={result['totalPayloadBytes']} "
          f"image-index={result['sourceIndexes']['imageItems']} audio-index={result['sourceIndexes']['audioItems']} "
          f"xml={len(result['sourceXmlFiles'])} built={result['build'].get('built', False)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

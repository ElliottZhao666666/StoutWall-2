#!/usr/bin/env python3
"""Extract source-window geometry and visible strings into a C++ layout manifest."""
from __future__ import annotations
import argparse, base64, json, struct, unicodedata
from pathlib import Path
from xml.etree import ElementTree as ET

PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT.parent
IGNORED = {"keye", "Clean", "Exui", "exui", "微软雅黑", "默认皮肤按钮",
           "Exui默认皮肤_图片框", "EX_UI默认皮肤_编辑框", "超仿QQ_选择框",
           "超仿QQ_单选框_圆", "模仿-360"}

def utf16_strings(data: bytes) -> list[str]:
    values = []
    # EXUI string members are UTF-16LE aligned to byte zero; scanning the
    # adjacent byte alignment mostly decodes image/color payload as Han text.
    for offset in (0,):
        run = []
        for i in range(offset, len(data) - 1, 2):
            c = chr(data[i] | (data[i + 1] << 8))
            cat = unicodedata.category(c)
            if cat[0] in "LNM" or c in " ./:-_()[]{}（）【】“”‘’，、。！？+%#@":
                run.append(c)
            else:
                value = "".join(run).strip()
                if len(value) >= 2 and not value.isdigit():
                    values.append(value)
                run = []
        value = "".join(run).strip()
        if len(value) >= 2 and not value.isdigit():
            values.append(value)
    return list(dict.fromkeys(values))

def extension(element: ET.Element) -> bytes:
    try:
        return base64.b64decode(element.get("扩展属性数据", "") or "", validate=True)
    except Exception:
        return b""

def _caption_candidate(data: bytes, offset: int) -> tuple[int, str] | None:
    """Read an EXUI length-prefixed UTF-16LE string at any byte offset."""
    if offset + 4 > len(data):
        return None
    size = struct.unpack_from("<I", data, offset)[0]
    # EXUI stores the byte count, including the UTF-16 NUL terminator.
    if size < 2 or size > 4096 or size % 2:
        return None
    end = offset + 4 + size
    if end > len(data):
        return None
    raw = data[offset + 4:end]
    if raw[-2:] != b"\0\0":
        return None
    try:
        value = raw[:-2].decode("utf-16le", errors="strict")
    except UnicodeDecodeError:
        return None
    if not value or len(value) < 2 or "\0" in value or "\ufffd" in value:
        return None
    if any(not (char in "\r\n\t" or char.isprintable()) for char in value):
        return None
    # The project is a Chinese/English desktop application. EXUI image and
    # skin blobs can decode as isolated Arabic/Cyrillic/private-use glyphs;
    # reject those while retaining CJK, full-width punctuation and ASCII.
    for char in value:
        codepoint = ord(char)
        if codepoint <= 0x7f:
            continue
        if 0x3400 <= codepoint <= 0x9fff:
            continue
        if 0x3000 <= codepoint <= 0x303f or 0xff00 <= codepoint <= 0xffef:
            continue
        if char in "\u00a9\u00ae\u2014\u2018\u2019\u201c\u201d\u2026\u00b7":
            continue
        return None
    if not any(char.isalnum() or unicodedata.category(char).startswith("L") for char in value):
        return None
    return size, value


def _caption_score(value: str, offset: int) -> int:
    score = len(value)
    score += sum(3 for char in value if "\u4e00" <= char <= "\u9fff")
    score += sum(2 for char in value if char.isalnum())
    # These are known EXUI skin/font metadata values, not user-facing text.
    if value in IGNORED or any(token.casefold() in value.casefold() for token in ("keye", "exui", "皮肤", "字体")):
        score -= 10000
    # Captions are normally stored after the skin metadata in the extension
    # blob. The offset is only a tie-breaker after text quality.
    return score * 10000 + offset


def get_caption(element: ET.Element) -> str:
    # Image controls carry binary payloads in the same extension field. Their
    # decoded bytes can resemble short CJK strings, but the original control
    # renders the image and does not display that payload as a caption.
    kind = ".".join(element.tag.split(".")[-2:])
    if kind in {"Lib3.4", "Lib3.7"} or any(token in element.tag for token in ("图片框", "图片按钮")):
        return ""
    explicit = element.get("标题", "")
    if explicit:
        return explicit
    data = extension(element)
    candidates = []
    for offset in range(0, max(0, len(data) - 3)):
        candidate = _caption_candidate(data, offset)
        if candidate is None:
            continue
        size, value = candidate
        candidates.append((_caption_score(value, offset), size, value))
    if not candidates:
        return ""
    candidates.sort(reverse=True)
    score, _size, value = candidates[0]
    return value if score > 0 else ""

def cppstr(value: str) -> str:
    return "L" + json.dumps(value, ensure_ascii=True)

def tab_titles(blob: bytes) -> list[str]:
    # In this snapshot each tab record contains a PNG followed by its title.
    titles = []
    marker = bytes.fromhex("49 45 4e 44 ae 42 60 82")
    start = 0
    while (end := blob.find(marker, start)) != -1:
        start = end + len(marker)
        title = _caption_candidate(blob, start + 8)
        if title:
            titles.append(title[1])
    return titles

def run(src: Path, out: Path):
    windows, controls, tabs = [], [], []
    for path in sorted(src.glob("*.xml"), key=lambda x: x.name.casefold()):
        root = ET.parse(path).getroot()
        wname = root.get("名称", path.stem)
        windows.append((wname, root.get("标题", wname), int(root.get("宽度", 0)), int(root.get("高度", 0)),
                        root.get("最小化按钮") == "真", root.get("底色") == "16777215"))
        def walk(parent: ET.Element, parent_name: str, parent_type: str = ""):
            for el in parent:
                name = el.get("名称", "")
                owner = parent_name
                kind = ".".join(el.tag.split(".")[-2:])
                if name and all(el.get(k) is not None for k in ("左边", "顶边", "宽度", "高度")):
                    blob = extension(el)
                    if kind == "Lib3.3":
                        titles = tab_titles(blob)
                        if not titles:
                            raise ValueError(f"Missing tab titles in {wname}/{name}")
                        tabs.extend((wname, name, i, title) for i, title in enumerate(titles))
                    page = -1
                    if parent_type == "Lib3.3":
                        # The direct child's second DWORD is a 1-based page number.
                        # Zero denotes controls that are not attached to a displayed page.
                        page = int.from_bytes(blob[4:8], "little") - 1
                        if page < 0:
                            page = -2
                    events = [child.get("处理器", "") for child in el if child.get("处理器")]
                    controls.append((wname, name, kind, int(el.get("左边")),
                                     int(el.get("顶边")), int(el.get("宽度")), int(el.get("高度")),
                                     get_caption(el), el.get("可视", "真") != "假", owner,
                                     ";".join(events), blob, page))
                    owner = name
                walk(el, owner, kind if owner == name else parent_type)
        walk(root, wname)
    lines = ["#pragma once", "#include <cstddef>", "namespace stoutwall {",
      "struct WindowDef { const wchar_t* name; const wchar_t* title; int width,height; bool minimize, white; };",
      "struct ControlDef { const wchar_t* window; const wchar_t* name; const wchar_t* type; int x,y,w,h; const wchar_t* text; bool visible; const wchar_t* parent; const wchar_t* events; int page; };",
      "struct TabDef { const wchar_t* window; const wchar_t* control; int index; const wchar_t* title; };",
      f"inline constexpr WindowDef OriginalWindows[{len(windows)}] = {{"]
    for n,t,w,h,m,white in windows:
        lines.append(f"    {{{cppstr(n)},{cppstr(t)},{w},{h},{str(m).lower()},{str(white).lower()}}},")
    lines.extend(["};", f"inline constexpr TabDef OriginalTabs[{len(tabs)}] = {{"])
    for window, control, index, title in tabs:
        lines.append(f"    {{{cppstr(window)},{cppstr(control)},{index},{cppstr(title)}}},")
    lines.extend(["};", f"inline constexpr ControlDef OriginalControls[{len(controls)}] = {{"])
    for x in controls:
        n,name,typ,l,t,w,h,text,visible,parent,events,_blob,page=x
        lines.append(f"    {{{cppstr(n)},{cppstr(name)},{cppstr(typ)},{l},{t},{w},{h},{cppstr(text)},{str(visible).lower()},{cppstr(parent)},{cppstr(events)},{page}}},")
    lines.extend(["};", "} // namespace stoutwall", ""])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    manifest = {"windows":[{"name":n,"title":t,"width":w,"height":h,"minimize":m,"white":white} for n,t,w,h,m,white in windows],
                "tabs":[{"window":w,"control":c,"index":i,"title":t} for w,c,i,t in tabs],
                "controls":[{"window":x[0],"name":x[1],"type":x[2],"bounds":x[3:7],"caption":x[7],"visible":x[8],"parent":x[9],"events":x[10],"extensionBytes":len(x[11]),"page":x[12]} for x in controls]}
    (out.parent / "OriginalLayout.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(f"windows={len(windows)} controls={len(controls)} output={out}")

if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",type=Path,default=SOURCE/"src")
    parser.add_argument("--output",type=Path,default=PROJECT/"src"/"OriginalLayout.hpp")
    args=parser.parse_args()
    run(args.source,args.output)

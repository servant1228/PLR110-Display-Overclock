#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
Extract symbol CRCs from a kernel module's __versions /
__version_ext_crcs + __version_ext_names sections and emit
Module.symvers lines.

Usage:
    extract_symvers.py <module.ko> [more.ko ...] > Module.symvers

Handles both classic CONFIG_MODVERSIONS
    __versions: struct modversion_info { unsigned long crc; char name[56]; }
and CONFIG_EXTENDED_MODVERSIONS
    __version_ext_crcs  : u32[]
    __version_ext_names : concatenated NUL-terminated names
"""
import struct
import sys


def sections(d):
    assert d[:4] == b"\x7fELF", "not an ELF"
    assert d[4] == 2, "not ELF64"
    e_shoff = struct.unpack_from("<Q", d, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", d, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", d, 0x3C)[0]
    e_shstrndx = struct.unpack_from("<H", d, 0x3E)[0]
    raw = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (name, typ, flags, addr, offset, size, link, info, align, entsize) = \
            struct.unpack_from("<IIQQQQIIQQ", d, off)
        raw.append(dict(name=name, typ=typ, off=offset, size=size, entsize=entsize))
    strtab = raw[e_shstrndx]
    out = {}
    for s in raw:
        o = strtab["off"] + s["name"]
        e = d.index(b"\0", o)
        s["sname"] = d[o:e].decode("utf-8", "replace")
        if s["sname"]:
            out[s["sname"]] = s
    return out


def parse(path):
    d = open(path, "rb").read()
    sec = sections(d)
    pairs = []

    ext_crcs = sec.get("__version_ext_crcs")
    ext_names = sec.get("__version_ext_names")
    if ext_crcs and ext_names:
        blob = d[ext_names["off"]:ext_names["off"] + ext_names["size"]]
        names = blob.split(b"\0")
        if names and names[-1] == b"":
            names.pop()
        n = ext_crcs["size"] // 4
        for i in range(min(n, len(names))):
            crc = struct.unpack_from("<I", d, ext_crcs["off"] + i * 4)[0]
            pairs.append((crc, names[i].decode("utf-8", "replace")))
        return pairs

    ver = sec.get("__versions")
    if ver:
        # classic layout: unsigned long crc; char name[MODULE_NAME_LEN];
        # MODULE_NAME_LEN in the kernel is 56, so entry size is 64 on LP64.
        ent = 64 if ver["size"] % 64 == 0 else 8 + 56
        for i in range(ver["size"] // ent):
            base = ver["off"] + i * ent
            crc = struct.unpack_from("<I", d, base)[0]
            nm = d[base + 8:base + 8 + 56].split(b"\0")[0]
            pairs.append((crc, nm.decode("utf-8", "replace")))
    return pairs


def main():
    seen = {}
    for path in sys.argv[1:]:
        try:
            pairs = parse(path)
        except Exception as exc:  # noqa: BLE001
            print(f"# {path}: {exc}", file=sys.stderr)
            continue
        mod = path.rsplit("/", 1)[-1].removesuffix(".ko")
        for crc, name in pairs:
            if not name:
                continue
            seen.setdefault(name, (crc, mod))
        print(f"# {path}: {len(pairs)} entries", file=sys.stderr)
    for name, (crc, mod) in sorted(seen.items()):
        print(f"0x{crc:08x}\t{name}\t{mod}")


if __name__ == "__main__":
    main()

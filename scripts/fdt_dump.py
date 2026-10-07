#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Minimal Flattened Device Tree parser + dumper.

Usage:
    fdt_dump.py <fdt|dtbo> list [substring]
    fdt_dump.py <fdt|dtbo> dump <node-path-fragment> [--json]
"""
import json
import struct
import sys

FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9


class Node:
    def __init__(self, name, parent=None):
        self.name = name
        self.parent = parent
        self.props = {}
        self.children = []

    @property
    def path(self):
        if self.parent is None:
            return "/"
        p = self.parent.path
        return (p.rstrip("/") + "/" + self.name) if self.name else p

    def find_all(self, sub):
        out = []
        if sub.lower() in self.name.lower():
            out.append(self)
        for c in self.children:
            out.extend(c.find_all(sub))
        return out


def parse(blob):
    magic, totalsize, off_struct, off_strings, off_rsvmap, version, last_comp, \
        boot_cpuid, size_strings, size_struct = struct.unpack_from(">10I", blob, 0)
    assert magic == 0xD00DFEED, "bad FDT magic"
    pos = off_struct
    strings = blob[off_strings:off_strings + size_strings]

    def cstr(buf, o):
        e = buf.index(b"\0", o)
        return buf[o:e].decode("utf-8", "replace")

    root = Node("", None)
    cur = root
    while True:
        tok = struct.unpack_from(">I", blob, pos)[0]
        pos += 4
        if tok == FDT_BEGIN_NODE:
            name = cstr(blob, pos)
            pos += (len(name.encode()) + 1 + 3) & ~3
            n = Node(name, cur)
            cur.children.append(n)
            cur = n
        elif tok == FDT_END_NODE:
            cur = cur.parent
        elif tok == FDT_PROP:
            length, nameoff = struct.unpack_from(">II", blob, pos)
            pos += 8
            val = blob[pos:pos + length]
            pos += (length + 3) & ~3
            pname = cstr(strings, nameoff)
            cur.props[pname] = val
        elif tok == FDT_NOP:
            pass
        elif tok == FDT_END:
            break
        else:
            raise ValueError(f"bad token {tok} @ {pos-4}")
    return root


def fmt(val, pretty=True):
    if len(val) == 0:
        return "<empty>"
    if val[-1] == 0 and all(32 <= c < 127 or c == 0 for c in val):
        s = val.rstrip(b"\0").decode("ascii", "replace")
        if s:
            return f'"{s}"'
    if len(val) % 4 == 0:
        words = struct.unpack(f">{len(val)//4}I", val)
        # u32 array with trailing u64 pair is common; show both u32 and u64 view
        return "u32[" + ", ".join(str(w) for w in words) + "]"
    return "hex:" + val.hex()


def fmt_full(val):
    out = [fmt(val)]
    if len(val) % 4 == 0 and len(val) >= 8:
        words = struct.unpack(f">{len(val)//4}I", val)
        if len(val) % 8 == 0:
            quad = struct.unpack(f">{len(val)//8}Q", val)
            out.append("  u64: " + ", ".join(str(q) for q in quad))
        out.append("  u32[be]: " + " ".join(f"{w:#010x}" for w in words))
    out.append("  bytes: " + " ".join(f"{b:02x}" for b in val[:256]))
    return "\n".join(out)


def node_json(n):
    return {
        "path": n.path,
        "props": {k: fmt(v) for k, v in n.props.items()},
        "children": [node_json(c) for c in n.children],
    }


def main():
    path, cmd = sys.argv[1], sys.argv[2]
    root = parse(open(path, "rb").read())
    if cmd == "list":
        sub = sys.argv[3] if len(sys.argv) > 3 else ""
        for n in root.find_all(sub):
            if not n.props and not n.children:
                continue
            print(n.path)
    elif cmd == "dump":
        sub = sys.argv[3]
        hits = root.find_all(sub)
        if not hits:
            print("no node matching", sub)
            return 1
        for n in hits:
            if len(sys.argv) > 4 and sys.argv[4] == "--json":
                print(json.dumps(node_json(n), indent=2, ensure_ascii=False))
                continue
            print(f"\n===== {n.path}  ({len(n.props)} props, {len(n.children)} children)")
            for k, v in sorted(n.props.items()):
                print(f"  {k} = {fmt_full(v)}")
            for c in n.children:
                print(f"  [child] {c.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

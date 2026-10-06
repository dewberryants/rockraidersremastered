"""List or extract LEGO Rock Raiders WAD archives (analysis helper).

Format (verified against LegoRR0/1.wad and Wad_Load @0048b4e0):
  char[4] "WWAD"; u32 count;
  count x cstring  relative path (lookup key, e.g. "Lego.cfg")
  count x cstring  original build-machine path (unused at runtime)
  count x { u32 flags; u32 size; u32 packedSize; u32 offset; }

Usage: python wad_extract.py <wad> [--list] [--out DIR]
"""
import argparse
import collections
import struct
from pathlib import Path


def read_wad(path):
    d = Path(path).read_bytes()
    if d[:4] != b"WWAD":
        raise ValueError(f"{path}: not a WWAD archive")
    count = struct.unpack_from("<I", d, 4)[0]
    p = 8

    def cstr():
        nonlocal p
        e = d.index(b"\0", p)
        s = d[p:e].decode("latin-1")
        p = e + 1
        return s

    names = [cstr() for _ in range(count)]
    [cstr() for _ in range(count)]
    entries = []
    for i in range(count):
        flags, size, packed, offset = struct.unpack_from("<4I", d, p + 16 * i)
        entries.append((names[i], flags, size, packed, offset))
    return d, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wad")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args()
    d, entries = read_wad(a.wad)
    print(f"{len(entries)} entries; flags histogram: {collections.Counter(e[1] for e in entries)}")
    if a.list:
        for name, flags, size, packed, offset in entries:
            print(f"{flags} {size:9} {packed:9} {offset:#010x} {name}")
    if a.out:
        out = Path(a.out)
        for name, flags, size, packed, offset in entries:
            if flags != 1:
                print("skipping non-stored entry", flags, name)
                continue
            target = out / name.replace("\\", "/")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(d[offset:offset + size])
        print("extracted to", out)


if __name__ == "__main__":
    main()

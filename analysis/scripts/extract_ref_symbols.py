"""Extract `// <LegoRR.exe @XXXXXXXX>` address annotations from the OpenLRR reference tree.

Only facts are taken (address, symbol name, kind, the declaration line as a hint); no code is copied.
Output: analysis/raw/ref_symbols.json  -> [{"address", "name", "kind": "func"|"data", "decl", "file"}]
"""
import json
import re
from pathlib import Path

WTF = Path(__file__).resolve().parents[2]
REF = WTF / "tools" / "refs" / "OpenLRR" / "src"
OUT = WTF / "analysis" / "raw" / "ref_symbols.json"

ANNOT = re.compile(r"<LegoRR\.exe @([0-9a-fA-F]{8})>")
# a function declaration/definition: "... Name(" ; a define-alias: "#define Name ((ret (cc*)(args))0x...)"
FUNC = re.compile(r"(?:^|[\s*&])([A-Za-z_][A-Za-z0-9_:]*)\s*\(")
DEFINE = re.compile(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s")
EXTERN = re.compile(r"extern\s+.*?[\s*&]([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])*\s*;")
SKIP = {"if", "while", "for", "switch", "return", "sizeof", "assert_sizeof", "__declspec", "static_assert"}


def main():
    syms = {}
    for path in sorted(REF.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp", ".c"):
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines):
            m = ANNOT.search(line)
            if not m:
                continue
            addr = m.group(1).lower()
            # the declaration is usually on one of the next few non-comment lines
            for nxt in lines[i + 1:i + 6]:
                s = nxt.strip().lstrip("/").strip()
                if not s or s.startswith("<LegoRR.exe"):
                    continue
                kind, name = None, None
                if (n := re.search(r"noinline\(([A-Za-z_][A-Za-z0-9_]*)\)", s)):
                    kind, name = "func", n.group(1)
                elif (d := DEFINE.match(s)):
                    kind, name = "func", d.group(1)
                elif (e := EXTERN.search(s)):
                    kind, name = "data", e.group(1)
                else:
                    for f in FUNC.finditer(s):
                        if f.group(1) not in SKIP and not f.group(1).startswith("__"):
                            kind, name = "func", f.group(1)
                            break
                if name:
                    prev = syms.get(addr)
                    # prefer header declarations (first seen) but keep any name if none yet
                    if prev is None:
                        syms[addr] = {"address": addr, "name": name.split("::")[-1], "kind": kind,
                                      "decl": s[:200], "file": str(path.relative_to(REF))}
                    break
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(sorted(syms.values(), key=lambda x: x["address"]), indent=1))
    kinds = {}
    for s in syms.values():
        kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
    print(len(syms), "symbols", kinds)


if __name__ == "__main__":
    main()

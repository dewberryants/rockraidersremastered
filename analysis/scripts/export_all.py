"""Bulk-export the current Ghidra analysis of LegoRR.exe through the GhidraMCP HTTP server.

Writes (git-ignored, derived from the original binary):
  analysis/raw/functions.json      - every function: name, address, xref count
  analysis/raw/strings.json        - every defined string
  analysis/raw/imports.json        - imported API symbols
  analysis/raw/decomp/<addr>_<name>.c  - decompiled C for each function
  analysis/raw/decomp_all.c        - all of the above concatenated (for grepping)

Usage: tools/ghidra-mcp/.venv/Scripts/python analysis/scripts/export_all.py [--only-renamed]
"""
import argparse
import json
import re
import sys
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HOST = "http://127.0.0.1:8089"
ROOT = Path(__file__).resolve().parents[1] / "raw"


def get(path, **params):
    url = f"{HOST}/{path}?{urllib.parse.urlencode(params)}"
    with urllib.request.urlopen(url, timeout=300) as r:
        return json.loads(r.read().decode("utf-8"))


def paged(path, key, page=1000, **params):
    out, offset = [], 0
    while True:
        res = get(path, offset=offset, limit=page, **params)
        items = res.get(key, [])
        out.extend(items)
        offset += len(items)
        if not items or offset >= res.get("total", 0):
            return out


def safe(name):
    return re.sub(r"[^A-Za-z0-9_.@-]", "_", name)[:120]


def decompile(fn):
    try:
        res = get("force_decompile", function=fn["address"])
        return fn, res.get("decompiled") or f"/* decompile failed: {res} */"
    except Exception as e:  # keep going; report at the end
        return fn, f"/* decompile error: {e} */"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only-renamed", action="store_true", help="skip FUN_ functions (faster refresh)")
    ap.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()

    (ROOT / "decomp").mkdir(parents=True, exist_ok=True)
    funcs = [f for f in paged("find_functions", "functions", name_pattern="") if not f.get("is_external")]
    funcs.sort(key=lambda f: int(f["address"], 16))
    (ROOT / "functions.json").write_text(json.dumps(funcs, indent=1))
    (ROOT / "strings.json").write_text(json.dumps(paged("list_strings", "strings"), indent=1))
    (ROOT / "imports.json").write_text(json.dumps(get("list_program_items", kind="imports").get("items", []), indent=1))
    print(f"{len(funcs)} functions", file=sys.stderr)

    todo = [f for f in funcs if not (args.only_renamed and f["name"].startswith("FUN_"))]
    for old in (ROOT / "decomp").glob("*.c"):
        old.unlink()
    results = {}
    with ThreadPoolExecutor(args.workers) as pool:
        for i, (fn, code) in enumerate(pool.map(decompile, todo), 1):
            results[fn["address"]] = (fn, code)
            (ROOT / "decomp" / f"{fn['address']}_{safe(fn['name'])}.c").write_text(code, encoding="utf-8")
            if i % 250 == 0:
                print(f"  {i}/{len(todo)}", file=sys.stderr)

    with open(ROOT / "decomp_all.c", "w", encoding="utf-8") as out:
        for addr in sorted(results, key=lambda a: int(a, 16)):
            fn, code = results[addr]
            out.write(f"\n//==== {fn['address']} {fn['name']} (xrefs={fn.get('xref_count')})\n{code}\n")
    failed = [a for a, (_, c) in results.items() if c.startswith("/* decompile")]
    print(f"done; {len(failed)} failures", file=sys.stderr)


if __name__ == "__main__":
    main()

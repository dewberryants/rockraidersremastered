"""Print decompiled C for functions by name, regex, or address via the GhidraMCP server.

Usage: python dc.py File_Open Config_Load 0x48b4e0
       python dc.py --re "^Config_"        (all functions matching a regex)
       python dc.py --list "^Config_"      (just list name/address)
"""
import json
import re
import sys
import urllib.parse
import urllib.request

HOST = "http://127.0.0.1:8089"


def get(path, **params):
    with urllib.request.urlopen(f"{HOST}/{path}?{urllib.parse.urlencode(params)}", timeout=300) as r:
        return json.loads(r.read().decode("utf-8"))


def all_functions():
    out, offset = [], 0
    while True:
        res = get("find_functions", name_pattern="", offset=offset, limit=2000)
        out += res["functions"]
        offset += len(res["functions"])
        if not res["functions"] or offset >= res["total"]:
            return out


def main():
    args = sys.argv[1:]
    sys.stdout.reconfigure(encoding="utf-8")
    if args and args[0] in ("--re", "--list"):
        pat = re.compile(args[1])
        funcs = sorted((f for f in all_functions() if pat.search(f["name"])), key=lambda f: f["address"])
        if args[0] == "--list":
            for f in funcs:
                print(f["address"], f["name"], f"xrefs={f['xref_count']}")
            return
        targets = [f["address"] for f in funcs]
    else:
        targets = args
    for t in targets:
        res = get("force_decompile", function=t)
        code = res.get("decompiled", json.dumps(res)).replace("\r\n", "\n")
        print(f"//==== {res.get('address', t)} {res.get('name', '')}")
        print(code.strip("\n"))
        print()


if __name__ == "__main__":
    main()

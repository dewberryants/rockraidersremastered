"""Apply analysis/raw/symbol_map.json to the Ghidra program via the GhidraMCP HTTP server.

- creates functions that auto-analysis missed ("plausible")
- renames default-named functions (FUN_/thunk_FUN_) to the mapped names; library names found by
  Ghidra's Function ID (CRT etc.) are kept
- stores the reference declaration as a plate comment (type hints for later typing passes)
"""
import json
import sys
import urllib.request
from pathlib import Path

HOST = "http://127.0.0.1:8089"
RAW = Path(__file__).resolve().parents[1] / "raw"


def post(path, body):
    req = urllib.request.Request(f"{HOST}/{path}", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode("utf-8"))


def main():
    mapping = json.loads((RAW / "symbol_map.json").read_text())
    current = {f["address"].lower(): f["name"] for f in json.loads((RAW / "functions.json").read_text())}
    stats = {"created": 0, "renamed": 0, "kept": 0, "failed": 0}
    for i, m in enumerate(mapping, 1):
        if m["status"] not in ("start", "plausible") or not m["ours"]:
            continue
        addr, name = m["ours"], m["name"]
        try:
            if m["status"] == "plausible":
                res = post("create_function", {"address": f"0x{addr}", "name": name})
                ok = res.get("success")
                stats["created" if ok else "failed"] += 1
                if not ok:
                    print("create failed", addr, name, res, file=sys.stderr)
                    continue
            else:
                old = current.get(addr, "")
                if old.startswith(("FUN_", "thunk_FUN_")):
                    res = post("rename_function", {"old_name": old, "new_name": name})
                    ok = res.get("status") == "success" or res.get("success")
                    stats["renamed" if ok else "failed"] += 1
                    if not ok:
                        print("rename failed", addr, old, name, res, file=sys.stderr)
                elif old != name:
                    stats["kept"] += 1
            post("set_comment", {"address": f"0x{addr}", "type": "plate",
                                 "comment": f"{name}\nref decl: {m['decl']}"})
        except Exception as e:
            stats["failed"] += 1
            print("error", addr, name, e, file=sys.stderr)
        if i % 250 == 0:
            print(i, stats, file=sys.stderr)
    post("save_program", {})
    print(stats)


if __name__ == "__main__":
    main()

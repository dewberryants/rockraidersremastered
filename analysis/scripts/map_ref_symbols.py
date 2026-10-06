"""Map reference (OpenLRR) function addresses onto our LegoRR.exe build.

Our build is the same source/link order as the reference build, but a few functions differ in size,
so addresses are shifted by a piecewise-constant delta. We recover the delta runs by matching
sequences of consecutive function-size gaps, then map every reference function and verify it.

Output: analysis/raw/symbol_map.json -> [{"ours", "ref", "name", "status"}]
  status: "start"      ours is already a Ghidra function start
          "plausible"  not a Ghidra function yet, but the bytes look like a function start
          "unmapped"   no confident location
"""
import collections
import json
import struct
from pathlib import Path

WTF = Path(__file__).resolve().parents[2]
RAW = WTF / "analysis" / "raw"
EXE = WTF / "Rock Raiders" / "LegoRR.exe"
K = 3          # gap-sequence length used for anchors
MIN_RUN = 3    # anchors needed before a delta run is trusted


def load_text():
    d = EXE.read_bytes()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    so = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    for i in range(nsec):
        s = d[so + i * 40:so + i * 40 + 40]
        if s[:5] == b".text":
            vs, va, rs, ro = struct.unpack_from("<IIII", s, 8)
            return 0x400000 + va, d[ro:ro + vs]
    raise SystemExit("no .text")


def delta_runs(G, R):
    gaps = lambda A: [A[i + 1] - A[i] for i in range(len(A) - 1)]
    gg, rg = gaps(G), gaps(R)
    idx = collections.defaultdict(list)
    for j in range(len(gg) - K + 1):
        idx[tuple(gg[j:j + K])].append(j)
    runs = []
    for i in range(len(rg) - K + 1):
        c = idx.get(tuple(rg[i:i + K]))
        if c and len(c) == 1:
            r, d = R[i], G[c[0]] - R[i]
            if runs and runs[-1][2] == d:
                runs[-1][1] = r
                runs[-1][3] += 1
            else:
                runs.append([r, r, d, 1])
    return [(a, b, d) for a, b, d, n in runs if n >= MIN_RUN]


def main():
    text_base, text = load_text()
    text_end = text_base + len(text)
    G = sorted(int(f["address"], 16) for f in json.loads((RAW / "functions.json").read_text()))
    Gset = set(G)
    ref = [s for s in json.loads((RAW / "ref_symbols.json").read_text())
           if s["kind"] == "func" and text_base <= int(s["address"], 16) < text_end]
    R = sorted(int(s["address"], 16) for s in ref)
    # both builds start .text with identical functions, so seed a zero-delta run at the start
    runs = [(text_base, text_base, 0)] + delta_runs(G, R)

    def byte(a):
        return text[a - text_base] if text_base <= a < text_end else None

    def looks_like_start(a):
        p1, p3 = byte(a - 1), byte(a - 3)
        if p1 is None:
            return False
        return p1 in (0xCC, 0x90, 0xC3) or p3 == 0xC2

    out = []
    for s in ref:
        r = int(s["address"], 16)
        before = [d for a, b, d in runs if a <= r]
        after = [d for a, b, d in runs if a > r]
        cands = []
        for d in (before[-1:] + after[:1]):
            if d not in cands:
                cands.append(d)
        status, ours = "unmapped", None
        for d in cands:
            if r + d in Gset:
                status, ours = "start", r + d
                break
        if ours is None:
            for d in cands:
                if looks_like_start(r + d):
                    status, ours = "plausible", r + d
                    break
        out.append({"ours": f"{ours:08x}" if ours else None, "ref": s["address"], "name": s["name"],
                    "status": status, "decl": s["decl"]})

    # the same Ghidra address must not get two names
    seen = collections.Counter(o["ours"] for o in out if o["ours"])
    for o in out:
        if o["ours"] and seen[o["ours"]] > 1:
            o["status"] = "conflict"
    (RAW / "symbol_map.json").write_text(json.dumps(out, indent=1))
    print("runs:", [(hex(a), hex(b), hex(d)) for a, b, d in runs][:3], "...", len(runs))
    print(collections.Counter(o["status"] for o in out))


if __name__ == "__main__":
    main()

"""Correctness checks: byte-identical to orjson (except NaN/Infinity, which follow std json),
std-json NaN tokens, NumPy == json.dumps(x.tolist()).  Run after ./build.sh:
    python tests/check_correctness.py [path/to/jsonexamples]
"""
import json, math, os, random, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build"))
import mojson, orjson
bad = 0
def as_orjson(b):  # our only intended difference from orjson: NaN/Infinity tokens
    return b.replace(b"-Infinity", b"null").replace(b"Infinity", b"null").replace(b"NaN", b"null")
def has_nonfinite(o):
    if isinstance(o, float): return not math.isfinite(o)
    if isinstance(o, dict): return any(has_nonfinite(v) for v in o.values())
    if isinstance(o, (list, tuple)): return any(has_nonfinite(v) for v in o)
    return False
def cmp(o, tag):
    global bad
    a, b = mojson.dumps(o), orjson.dumps(o)
    if has_nonfinite(o):   # only then may the output differ (NaN/Infinity tokens vs null)
        a = as_orjson(a)
    if a != b:
        bad += 1
        if bad < 5: print("DIFF", tag, a[:100], b[:100])
corpus = sys.argv[1] if len(sys.argv) > 1 else None
if corpus:
    for f in sorted(os.listdir(corpus)):
        if f.endswith(".json"): cmp(json.load(open(os.path.join(corpus, f), encoding="utf-8")), f)
random.seed(61)
fl = [struct.unpack("<d", struct.pack("<Q", random.getrandbits(64)))[0] for _ in range(300000)]
fl = [x for x in fl if math.isfinite(x)] + [2.0**e for e in range(-1074, 1024)] + [5e-324, 0.0, -0.0, 1e300, 0.1]
cmp(fl, "float list")
for x in fl[:100000]: cmp(x, "float")
ints = [random.getrandbits(63) * random.choice([1, -1]) for _ in range(50000)] + list(range(-50000, 50000)) + [2**63 - 1, -2**63]
cmp(ints, "int list")
alpha = [chr(c) for c in range(128)] + ["é", "漢", "😀"]
for _ in range(20000):
    s = "".join(random.choice(alpha) if random.random() < 0.05 else "a" for _ in range(random.randint(0, 300)))
    cmp(s, "string")
pool = [lambda: random.randint(-10**9, 10**9), lambda: 2**40, lambda: None, lambda: True, lambda: random.uniform(-1e6, 1e6),
        lambda: float("nan"), lambda: "s\\n", lambda: [1, 2.5], lambda: {"k": 1.25}, lambda: {}, lambda: []]
for _ in range(20000):
    L = [random.choice(pool)() for _ in range(random.randint(0, 40))]
    cmp(L, "fuzz list"); cmp({"k%d" % i: v for i, v in enumerate(L)}, "fuzz dict")
# std-json NaN/Infinity tokens
assert mojson.dumps([math.nan, math.inf, -math.inf]) == json.dumps([math.nan, math.inf, -math.inf], separators=(",", ":")).encode()
# NumPy (optional)
try:
    import numpy as np
    canon = lambda b: json.dumps(json.loads(b))
    rng = np.random.default_rng(9)
    for _ in range(300):
        n = int(rng.integers(0, 2000))
        a = rng.normal(size=n) * 10.0 ** rng.integers(-30, 30, size=n); a[rng.random(n) < 0.1] = np.nan; a[rng.random(n) < 0.03] = np.inf
        for arr in (a, a.astype(np.float32), rng.integers(-10**15, 10**15, n), rng.random(n) < 0.5, a.reshape(-1, 1) if n else a, a[::2]):
            if canon(mojson.dumps(arr)) != json.dumps(arr.tolist()): bad += 1
    print("numpy checks done")
except ImportError:
    print("numpy not installed, skipped")
for b in [{1: 2}, {"x": object()}, 2**64]:
    try: mojson.dumps(b); bad += 1; print("no error for", b)
    except Exception: pass
print("mismatches:", bad)
sys.exit(1 if bad else 0)

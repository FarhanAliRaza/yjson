"""mojson vs orjson(OPT_SERIALIZE_NUMPY) vs json.dumps(a.tolist()) on NumPy arrays."""
import json, os, sys, timeit
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build"))
import mojson, orjson, numpy as np
rng = np.random.default_rng(0)
cases = {"f64 1M": rng.normal(size=1_000_000), "f64 1M 5% NaN": np.where(rng.random(1_000_000) < 0.05, np.nan, rng.normal(size=1_000_000)),
         "f64 1000x1000": rng.random((1000, 1000)), "f32 1M": rng.random(1_000_000).astype(np.float32),
         "i64 1M +-1e6": rng.integers(-10**6, 10**6, 1_000_000), "i64 1M +-1e15": rng.integers(-10**15, 10**15, 1_000_000), "bool 1M": rng.random(1_000_000) < 0.5}
print(f"{'case':16}{'mojson ms':>10}{'orjson ms':>10}{'json ms':>9}")
for n, a in cases.items():
    t = [min(timeit.repeat(f, number=1, repeat=7)) * 1e3 for f in (lambda: mojson.dumps(a), lambda: orjson.dumps(a, option=orjson.OPT_SERIALIZE_NUMPY), lambda: json.dumps(a.tolist()))]
    print(f"{n:16}{t[0]:10.2f}{t[1]:10.2f}{t[2]:9.1f}")

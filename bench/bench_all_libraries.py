"""yjson vs orjson, msgspec, ujson, python-rapidjson, json, simplejson (correctness first, then speed).
    pip install orjson msgspec ujson python-rapidjson simplejson
    python bench/bench_all_libraries.py path/to/jsonexamples
Note: results depend on which library ran just before (allocator/cache state); order is rotated each round,
but prefer bench_paired.py for yjson-vs-orjson numbers."""
import json, math, os, sys, timeit
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build"))
import yjson, orjson, msgspec, ujson, rapidjson, simplejson
enc = msgspec.json.Encoder()
libs = {"yjson": yjson.dumps, "orjson": orjson.dumps, "msgspec": enc.encode,
        "ujson": lambda o: ujson.dumps(o, ensure_ascii=False, escape_forward_slashes=False),
        "rapidjson": lambda o: rapidjson.dumps(o, ensure_ascii=False),
        "json": lambda o: json.dumps(o, ensure_ascii=False, separators=(",", ":")),
        "simplejson": lambda o: simplejson.dumps(o, ensure_ascii=False, separators=(",", ":"))}
d = sys.argv[1]
files = sorted(f[:-5] for f in os.listdir(d) if f.endswith(".json"))
data = {f: json.load(open(os.path.join(d, f + ".json"), encoding="utf-8")) for f in files}
for f, obj in data.items():
    assert yjson.dumps(obj) == orjson.dumps(obj), f
    for n, fn in libs.items(): assert json.loads(fn(obj)) == obj, (n, f)
print("correctness OK\n")
res = {}
for f, obj in data.items():
    num = max(1, int(400_000 / len(orjson.dumps(obj))))
    t = {n: [] for n in libs}; order = list(libs.items())
    for rnd in range(21):
        for n, fn in order[rnd % len(order):] + order[:rnd % len(order)]:
            t[n].append(timeit.timeit(lambda: fn(obj), number=num) / num)
    res[f] = {n: min(v) for n, v in t.items()}
names = list(libs)
print(f"{'speedup vs orjson':18}" + "".join(f"{n:>11}" for n in names))
for f in files: print(f"{f:18}" + "".join(f"{res[f]['orjson']/res[f][n]:10.2f}x" for n in names))
print(f"{'GEOMEAN':18}" + "".join(f"{math.exp(sum(math.log(res[f]['orjson']/res[f][n]) for f in files)/len(files)):10.2f}x" for n in names))

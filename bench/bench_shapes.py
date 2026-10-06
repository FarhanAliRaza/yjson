"""Where does yjson win or lose against orjson? Paired timings per payload shape.

Synthetic payloads isolate one code path each (short float lists, dict keys,
string lengths, ...). Prints nanoseconds per call and the orjson/yjson ratio
(>1 = yjson faster). Use --filter to run a subset.

    python bench/bench_shapes.py --cpu 2 [--pairs 20] [--filter dict]
"""
import argparse
import gc
import os
from pathlib import Path
import random
import statistics as st
import sys
import timeit

_pre = argparse.ArgumentParser(add_help=False)
_pre.add_argument("--build", default=str(Path(__file__).resolve().parents[1] / "build"),
                  help="Directory holding the yjson build to measure (default: build/)")
sys.path.insert(0, _pre.parse_known_args()[0].build)
import yjson
import orjson


def shapes():
    rng = random.Random(7)
    words = ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa",
             "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau", "upsilon"]
    coords = [[rng.uniform(-180, 180), rng.uniform(-90, 90)] for _ in range(10000)]
    yield "float pairs x10k (canada)", coords
    yield "float pairs as tuples x10k", [tuple(pair) for pair in coords]
    yield "floats x10k flat", [c for pair in coords for c in pair]
    yield "short floats x10k", [round(rng.uniform(0, 1000), 2) for _ in range(10000)]
    yield "small ints x10k", [rng.randint(0, 1000) for _ in range(10000)]
    yield "large ints x10k", [rng.randint(10**12, 10**15) for _ in range(10000)]
    yield "mixed scalars x10k", [rng.choice([1, 2.5, None, True, "x"]) for _ in range(10000)]
    for size in (8, 32, 128, 1024):
        yield f"ascii str {size} x2k", ["a" * size for _ in range(2000)]
    yield "ascii str 128 escapes x2k", ['a"b\\c\n' * 21 for _ in range(2000)]
    yield "unicode str 32 x2k", ["é" * 16 for _ in range(2000)]
    yield "dict 3 str keys x2k", [{"id": i, "name": "Ada", "ok": True} for i in range(2000)]
    yield "dict 20 str keys x500", [{w: i for w in words} for i in range(500)]
    yield "dict 20 str->str x500", [{w: w.upper() * 3 for w in words} for _ in range(500)]
    yield "dict 100 int values x100", [{f"key{j}": j for j in range(100)} for _ in range(100)]
    yield "nested dict depth 6 x500", [{"a": {"b": {"c": {"d": {"e": {"f": i}}}}}} for i in range(500)]
    yield "list of empty dicts x10k", [{} for _ in range(10000)]
    yield "list of empty lists x10k", [[] for _ in range(10000)]
    yield "bools and none x10k", [rng.choice([True, False, None]) for _ in range(10000)]
    yield "wide dict 10k keys", {f"k{i}": i for i in range(10000)}
    yield "wide dict 10k str values", {f"k{i}": f"value number {i}" for i in range(10000)}
    yield "dict with float pair values x2k", [{"p": [rng.uniform(0, 1), rng.uniform(0, 1)], "id": i} for i in range(2000)]
    yield "str keys sorted x500", [{w: i for w in words} for i in range(500)]
    yield "nonstr keys unsorted x500", [{j: j for j in range(20)} for _ in range(500)]
    yield "nonstr keys sorted x500", [{j: j for j in range(20)} for _ in range(500)]
    yield "indent 2 dict 20 keys x500", [{w: i for w in words} for i in range(500)]


def measure(obj, pairs, batch_seconds, option=0):
    assert yjson.dumps(obj, option=option) == orjson.dumps(obj, option=option)
    timers = [timeit.Timer(lambda: orjson.dumps(obj, option=option)), timeit.Timer(lambda: yjson.dumps(obj, option=option))]
    for timer in timers:
        timer.timeit(5)
    number = 1
    while min(timer.timeit(number) for timer in timers) < batch_seconds:
        number *= 2
    samples = []
    for pair in range(pairs):
        times = [0.0, 0.0]
        for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
            times[index] = timers[index].timeit(number) / number
        samples.append(times)
    orjson_ns = st.median(t[0] for t in samples) * 1e9
    yjson_ns = st.median(t[1] for t in samples) * 1e9
    ratios = [t[0] / t[1] for t in samples]
    return orjson_ns, yjson_ns, st.median(ratios), st.quantiles(ratios, n=4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pairs", type=int, default=20)
    parser.add_argument("--batch-ms", type=float, default=10)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--filter", default="")
    parser.add_argument("--build", help="Directory holding the yjson build to measure (default: build/)")
    parser.add_argument("--output", type=Path, help="Save the table as JSON")
    args = parser.parse_args()
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    print(f"CPython {sys.version.split()[0]}, orjson {orjson.__version__}, {yjson.__file__}")
    print(f"{'shape':32} {'orjson us':>10} {'yjson us':>10} {'ratio':>7}  [p25 - p75]", flush=True)
    rows = []
    for name, obj in shapes():
        if args.filter not in name:
            continue
        option = 0
        if name.startswith("nonstr"):
            option = yjson.OPT_NON_STR_KEYS | (yjson.OPT_SORT_KEYS if "sorted" in name else 0)
        elif name.startswith("str keys sorted"):
            option = yjson.OPT_SORT_KEYS
        elif name.startswith("indent"):
            option = yjson.OPT_INDENT_2
        gc.collect()
        orjson_ns, yjson_ns, ratio, quartiles = measure(obj, args.pairs, args.batch_ms / 1000, option)
        print(f"{name:32} {orjson_ns / 1000:10.2f} {yjson_ns / 1000:10.2f} {ratio:6.2f}x  [{quartiles[0]:.2f} - {quartiles[2]:.2f}]", flush=True)
        rows.append({"name": name, "option": option, "orjson_us": orjson_ns / 1000, "yjson_us": yjson_ns / 1000,
                     "ratio": ratio, "p25": quartiles[0], "p75": quartiles[2]})
    if args.output:
        import json
        import platform
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"python": platform.python_version(), "orjson": orjson.__version__,
                                           "yjson_path": yjson.__file__, "pairs": args.pairs, "batch_ms": args.batch_ms,
                                           "results": rows}, indent=2) + "\n")


if __name__ == "__main__":
    main()

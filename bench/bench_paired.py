"""Paired, alternating mojson/orjson measurements with calibrated batches.

    python bench/bench_paired.py path/to/jsonexamples --output build/paired.json
"""
import argparse
import gc
import json
import math
import os
from pathlib import Path
import platform
import statistics as st
import sys
import timeit

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import mojson
import orjson


def measure(name, obj, pairs, batch_seconds):
    expected = orjson.dumps(obj)
    if mojson.dumps(obj) != expected:
        raise ValueError(f"Output mismatch: {name}")
    timers = [timeit.Timer(lambda: orjson.dumps(obj)), timeit.Timer(lambda: mojson.dumps(obj))]
    for timer in timers:
        timer.timeit(10)
    number = 1
    while True:
        elapsed = min(timer.timeit(number) for timer in timers)
        if elapsed >= batch_seconds:
            break
        number *= min(10, max(2, math.ceil(batch_seconds / max(elapsed, 1e-9))))
    samples = []
    for pair in range(pairs):
        times = [0.0, 0.0]
        for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
            times[index] = timers[index].timeit(number) / number
        samples.append({"orjson_seconds": times[0], "mojson_seconds": times[1], "ratio": times[0] / times[1]})
    ratios = [sample["ratio"] for sample in samples]
    quartiles = st.quantiles(ratios, n=4)
    return {"name": name, "bytes": len(expected), "iterations_per_batch": number,
            "orjson_us": st.median(s["orjson_seconds"] for s in samples) * 1e6,
            "mojson_us": st.median(s["mojson_seconds"] for s in samples) * 1e6,
            "ratio": st.median(ratios), "p25": quartiles[0], "p75": quartiles[2], "samples": samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--pairs", type=int, default=40)
    parser.add_argument("--batch-ms", type=float, default=10)
    parser.add_argument("--cpu", type=int, help="Pin this process to one logical CPU")
    parser.add_argument("--micro", action="store_true", help="Also measure small scalar/container payloads")
    parser.add_argument("--output", type=Path, help="Save metadata and all paired samples as JSON")
    args = parser.parse_args()
    if args.pairs < 4 or args.batch_ms <= 0:
        parser.error("Use at least 4 pairs and a positive batch duration")
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    cases = [(path.stem, json.loads(path.read_text(encoding="utf-8"))) for path in sorted(args.corpus.glob("*.json"))]
    if not cases:
        parser.error("Corpus contains no JSON files")
    corpus_count = len(cases)
    if args.micro:
        cases.extend([("small dict", {"id": 1, "name": "Ada", "ok": True}),
                      ("empty dict", {}), ("int", 123456789), ("float", 0.1),
                      ("short string", "hello world"), ("mixed list", [1, 2.5, None, "x", True])])
    print(f"CPython {platform.python_version()}, orjson {orjson.__version__}, {args.pairs} pairs, >= {args.batch_ms:g} ms/batch", flush=True)
    print(f"{'file':20} {'orjson us':>11} {'mojson us':>11} {'ratio':>8}   [p25 - p75]", flush=True)
    results = []
    for name, obj in cases:
        gc.collect()
        result = measure(name, obj, args.pairs, args.batch_ms / 1000)
        results.append(result)
        print(f"{name:20} {result['orjson_us']:11.3f} {result['mojson_us']:11.3f} {result['ratio']:7.2f}x   [{result['p25']:.2f} - {result['p75']:.2f}]", flush=True)
    geomean = st.geometric_mean(result["ratio"] for result in results[:corpus_count])
    print(f"CORPUS GEOMEAN: {geomean:.3f}x (>1 = mojson faster)", flush=True)
    if args.output:
        report = {"python": platform.python_version(), "platform": platform.platform(),
                  "orjson": orjson.__version__, "mojson_path": mojson.__file__,
                  "affinity": sorted(os.sched_getaffinity(0)), "pairs": args.pairs,
                  "batch_ms": args.batch_ms, "corpus": str(args.corpus.resolve()),
                  "corpus_geomean": geomean, "results": results}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

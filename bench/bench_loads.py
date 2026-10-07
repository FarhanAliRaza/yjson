"""Paired, alternating yjson.loads/orjson.loads measurements on the corpus documents.

    python bench/bench_loads.py build/jsonexamples [--pairs 40] [--json]

Reports the median time per call for each library and the median orjson/yjson time
ratio; values above 1 mean yjson decodes faster. With --json, every library that is
installed among msgspec and the standard library's json is timed as well.
"""
import argparse
import gc
import json
import math
from pathlib import Path
import statistics as st
import sys
import timeit

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import yjson
import orjson


def decoders(include_others):
    result = {"orjson": orjson.loads, "yjson": yjson.loads}
    if include_others:
        result["json"] = json.loads
        try:
            import msgspec
        except ImportError:
            pass
        else:
            result["msgspec"] = msgspec.json.decode
    return result


def measure(data, functions, pairs, batch_seconds):
    expected = orjson.loads(data)
    for name, fn in functions.items():
        if fn(data) != expected:
            raise ValueError(f"{name} decoded a different value")
    timers = {name: timeit.Timer(lambda fn=fn: fn(data)) for name, fn in functions.items()}
    for timer in timers.values():
        timer.timeit(3)
    number = 1
    while True:
        elapsed = min(timer.timeit(number) for timer in timers.values())
        if elapsed >= batch_seconds:
            break
        number *= min(10, max(2, math.ceil(batch_seconds / max(elapsed, 1e-9))))
    samples = {name: [] for name in functions}
    names = list(functions)
    for pair in range(pairs):
        order = names if pair % 2 == 0 else names[::-1]
        for name in order:
            samples[name].append(timers[name].timeit(number) / number)
    medians = {name: st.median(times) for name, times in samples.items()}
    ratios = [o / y for o, y in zip(samples["orjson"], samples["yjson"])]
    return medians, st.median(ratios)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--pairs", type=int, default=40)
    parser.add_argument("--batch-seconds", type=float, default=0.01)
    parser.add_argument("--json", action="store_true", help="also time json and msgspec when installed")
    args = parser.parse_args()
    functions = decoders(args.json)
    gc.disable()
    width = max(len(name) for name in functions)
    print(f"{'document':<22} {'size':>8}  " + "  ".join(f"{name:>{max(width, 10)}}" for name in functions) + "  orjson/yjson")
    ratios = []
    for path in sorted(args.corpus.glob("*.json")):
        data = path.read_bytes()
        medians, ratio = measure(data, functions, args.pairs, args.batch_seconds)
        ratios.append(ratio)
        cells = "  ".join(f"{medians[name] * 1e6:>{max(width, 10) - 3}.1f} µs" for name in functions)
        print(f"{path.name:<22} {len(data) / 1024:>7.0f}K  {cells}  {ratio:>11.2f}×")
    if ratios:
        print(f"geometric mean of orjson/yjson: {math.exp(sum(map(math.log, ratios)) / len(ratios)):.2f}×")


if __name__ == "__main__":
    main()

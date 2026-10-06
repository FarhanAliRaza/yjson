"""Paired integer-rejection timings against a saved binary and orjson."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import sys
import timeit

from bench_regression import extension_path, load


def reject(dumps, value):
    try:
        dumps(value)
    except TypeError:
        return
    raise AssertionError("Integer outside the supported range was accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=extension_path("build/special-before"))
    parser.add_argument("--output", type=Path, default=Path("build/integer-errors.json"))
    args = parser.parse_args()
    os.sched_setaffinity(0, {2})
    current_path = extension_path("build")
    sys.path.insert(0, str(current_path.resolve().parent))
    before, current = load(args.baseline), load(current_path)
    import orjson
    results = []
    for value in (2**64, 2**100, -(2**63) - 1):
        for label, reference in (("previous", before), ("orjson", orjson)):
            timers = [timeit.Timer(lambda: reject(reference.dumps, value)),
                      timeit.Timer(lambda: reject(current.dumps, value))]
            for timer in timers:
                timer.timeit(20)
            number = 1
            while min(timer.timeit(number) for timer in timers) < .025:
                number *= 2
            samples = []
            for pair in range(40):
                times = [0, 0]
                for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
                    times[index] = timers[index].timeit(number) / number
                samples.append({"reference": times[0], "current": times[1],
                                "ratio": times[0] / times[1]})
            ratios = [s["ratio"] for s in samples]
            q1, _, q3 = statistics.quantiles(ratios, n=4)
            old = statistics.median(s["reference"] for s in samples) * 1e9
            new = statistics.median(s["current"] for s in samples) * 1e9
            ratio = statistics.median(ratios)
            print(f"{value} / {label}: {old:.0f} -> {new:.0f} ns, "
                  f"{ratio:.2f}x [{q1:.2f}, {q3:.2f}]", flush=True)
            results.append({"value": str(value), "reference": label,
                            "reference_ns": old, "current_ns": new,
                            "ratio": ratio, "p25": q1, "p75": q3, "samples": samples})
    args.output.write_text(json.dumps({
        "cpu": 2, "pairs": 40, "batch_ms": 25,
        "current_sha256": hashlib.sha256(current_path.read_bytes()).hexdigest(),
        "baseline_sha256": hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
        "results": results,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()

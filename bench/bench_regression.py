"""Compare saved old/new binaries in one process using alternating pairs."""
import argparse
import gc
import hashlib
import importlib.util
import json
from pathlib import Path
import os
import platform
import statistics as st
import sys
import sysconfig
import timeit


def extension_path(directory):
    """The build for this interpreter (build.sh names it yjson<EXT_SUFFIX>), else a plain yjson.so."""
    directory = Path(directory)
    tagged = directory / f"yjson{sysconfig.get_config_var('EXT_SUFFIX')}"
    return tagged if tagged.exists() else directory / "yjson.so"


def load(path):
    spec = importlib.util.spec_from_file_location("yjson", path.resolve())
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=extension_path("build/baseline"))
    parser.add_argument("--current", type=Path, default=extension_path("build"))
    parser.add_argument("--corpus", type=Path, default=Path("build/jsonexamples"))
    parser.add_argument("--output", type=Path, default=Path("build/regression.json"))
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--pairs", type=int, default=40)
    parser.add_argument("--batch-ms", type=float, default=10)
    args = parser.parse_args()
    os.sched_setaffinity(0, {args.cpu})
    sys.path.insert(0, str(args.current.resolve().parent))
    before, after = load(args.baseline), load(args.current)
    cases = [(p.stem, json.loads(p.read_text())) for p in sorted(args.corpus.glob("*.json"))]
    corpus_count = len(cases)
    cases.extend([("small dict", {"id": 1, "name": "Ada", "ok": True}), ("empty dict", {}),
                  ("int", 123456789), ("float", 0.1), ("short string", "hello world"),
                  ("mixed list", [1, 2.5, None, "x", True])])
    results = []
    print(f"{'case':20} {'before us':>11} {'after us':>11} {'speedup':>9}  [p25 - p75]", flush=True)
    for name, obj in cases:
        gc.collect()
        assert before.dumps(obj) == after.dumps(obj), name
        timers = [timeit.Timer(lambda: before.dumps(obj)), timeit.Timer(lambda: after.dumps(obj))]
        for timer in timers:
            timer.timeit(10)
        number = 1
        while min(timer.timeit(number) for timer in timers) < args.batch_ms / 1000:
            number *= 2
        samples = []
        for pair in range(args.pairs):
            times = [0, 0]
            for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
                times[index] = timers[index].timeit(number) / number
            samples.append({"before": times[0], "after": times[1], "ratio": times[0] / times[1]})
        ratio = st.median(s["ratio"] for s in samples)
        q1, _, q3 = st.quantiles([s["ratio"] for s in samples], n=4)
        old = st.median(s["before"] for s in samples) * 1e6
        new = st.median(s["after"] for s in samples) * 1e6
        print(f"{name:20} {old:11.3f} {new:11.3f} {ratio:8.3f}x  [{q1:.3f} - {q3:.3f}]", flush=True)
        results.append({"name": name, "before_us": old, "after_us": new, "ratio": ratio,
                        "p25": q1, "p75": q3, "samples": samples})
    geomean = st.geometric_mean(r["ratio"] for r in results[:corpus_count])
    print(f"CORPUS GEOMEAN: {geomean:.3f}x (>1 = new build faster)", flush=True)
    args.output.write_text(json.dumps({"baseline": str(args.baseline), "current": str(args.current),
                                      "baseline_sha256": hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
                                      "current_sha256": hashlib.sha256(args.current.read_bytes()).hexdigest(),
                                      "python": platform.python_version(), "cpu": args.cpu,
                                      "pairs": args.pairs, "batch_ms": args.batch_ms,
                                      "gc": "timeit disables GC during timed batches",
                                      "corpus_geomean": geomean, "results": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()

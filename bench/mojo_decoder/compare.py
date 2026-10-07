"""The Mojo decoder against the C one: identical results on the corpus, then alternating timings.

    python bench/mojo_decoder/compare.py [build/jsonexamples] [--pairs N]

Checks corpus documents and edge cases against orjson, then measures alternating
batches of shipping yjson.loads, bench/c_decoder and orjson.loads. --ctypes also measures the old Python wrapper.
"""
import argparse
import atexit
import ctypes
import gc
import importlib.util
import json
import os
import platform
import math
import statistics as st
import sys
import timeit
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(HERE), str(ROOT / "bench" / "c_decoder"), str(ROOT / "build")]
import _c_decoder
import orjson
import yjson

lib = ctypes.PyDLL(yjson.__file__)  # PyDLL: the decoder needs the GIL held
lib.yjson_mojo_init.restype = ctypes.c_void_p
lib.yjson_mojo_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
lib.yjson_mojo_loads.restype = ctypes.c_void_p
lib.yjson_mojo_loads.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p]
context = lib.yjson_mojo_init(id(True), id(False), id(None))
error = (ctypes.c_ssize_t * 2)()
error_address = ctypes.addressof(error)
ctypes.pythonapi.Py_DecRef.argtypes = [ctypes.c_void_p]


lib.yjson_mojo_destroy.argtypes = [ctypes.c_void_p]
lib.yjson_mojo_destroy.restype = None
atexit.register(lib.yjson_mojo_destroy, context)
mojo_loads = yjson.loads


def ctypes_loads(data):
    if isinstance(data, str):
        data = data.encode()
    buffer = ctypes.c_char_p(data)
    result = lib.yjson_mojo_loads(context, ctypes.cast(buffer, ctypes.c_void_p).value, len(data), error_address)
    if result is None:
        if error[0] == 0:  # only MemoryError is raised as a Python exception
            ctypes.pythonapi.PyErr_Clear()
            raise MemoryError
        raise ValueError(f"decode error {error[0]} at {error[1]}")
    obj = ctypes.cast(result, ctypes.py_object).value  # a second reference
    ctypes.pythonapi.Py_DecRef(result)  # drop the one the decoder returned
    return obj


def same(a, b):
    if type(a) is not type(b):
        return False
    if isinstance(a, float):
        return a == b and math.copysign(1, a) == math.copysign(1, b)
    if isinstance(a, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict):
        return list(a) == list(b) and all(same(a[k], b[k]) for k in a)
    return a == b


def compact_json(data):
    """Remove formatting whitespace while preserving strings and number spelling."""
    out = bytearray()
    quoted = escaped = False
    for byte in data:
        if quoted:
            out.append(byte)
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
            out.append(byte)
        elif byte not in (9, 10, 13, 32):
            out.append(byte)
    return bytes(out)


EDGE_CASES = ["", " ", "[", "[1,]", "{}", "[]", '""', "1e400", '"\\ud800"', b"\xef\xbb\xbf{}", "[" * 1025 + "]" * 1025, "tru",
              "truee", '{"a":}', "01", "-", "1.", "[1 2]", b'"\xff"', "nul", "nulll", '{"caf\\u00e9": [null, true, false, 1.5e3, -0]}',
              "[18446744073709551615, 18446744073709551616, -9223372036854775808, 0.1234567890123456789, 1.00000000000000000000]"]


def measure(data, functions, pairs, batch_seconds):
    timers = {name: timeit.Timer(lambda fn=fn: fn(data)) for name, fn in functions.items()}
    for timer in timers.values():
        timer.timeit(10)
    number = 1
    while min(timer.timeit(number) for timer in timers.values()) < batch_seconds:
        number *= 2
    samples = {name: [] for name in functions}
    names = list(functions)
    for pair in range(pairs):
        offset = pair % len(names)
        order = names[offset:] + names[:offset]
        if pair % 2:
            order = order[::-1]
        for name in order:
            samples[name].append(timers[name].timeit(number) / number)
    medians = {name: st.median(times) for name, times in samples.items()}
    ratios = [m / c for m, c in zip(samples["Mojo"], samples["C"])]
    return {"iterations_per_batch": number, "seconds": medians, "samples": samples,
            "mojo_over_c": st.median(ratios),
            "ratio_p25": st.quantiles(ratios, n=4)[0],
            "ratio_p75": st.quantiles(ratios, n=4)[2]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="?", type=Path, default=ROOT / "build" / "jsonexamples")
    parser.add_argument("--pairs", type=int, default=40)
    parser.add_argument("--batch-ms", type=float, default=10)
    parser.add_argument("--cpu", type=int, help="Pin the process to one logical CPU")
    parser.add_argument("--output", type=Path, help="Save metadata and every paired sample as JSON")
    parser.add_argument("--ctypes", action="store_true", help="Also measure the Python ctypes wrapper")
    parser.add_argument("--compact", action="store_true", help="Also measure corpus files with formatting whitespace removed")
    parser.add_argument("--baseline", type=Path, help="Native extension file for a saved Mojo baseline")
    args = parser.parse_args()
    if args.pairs < 4 or args.batch_ms <= 0:
        parser.error("Use at least 4 pairs and a positive batch duration")
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    sys.setrecursionlimit(20000)
    docs = {p.name: p.read_bytes() for p in sorted(args.corpus.glob("*.json"))}
    if not docs:
        parser.error("Corpus contains no JSON files")
    corpus_count = len(docs)
    record = {"id": 1, "name": "Alice Example", "email": "alice@example.com", "active": True, "score": 97.5,
              "tags": ["admin", "staff"], "address": {"city": "Lahore", "zip": "54000"}, "balance": 1234567, "note": None}
    docs["1000 records"] = orjson.dumps([dict(record, id=i) for i in range(1000)])
    if args.compact:
        for name, data in list(docs.items())[:corpus_count]:
            compact = compact_json(data)
            if not same(orjson.loads(compact), orjson.loads(data)):
                raise SystemExit(f"{name}: compact variant changed the decoded value")
            docs[name + " (compact)"] = compact
    functions = {"C": _c_decoder.loads, "Mojo": mojo_loads, "orjson": orjson.loads}
    if args.baseline:
        path = args.baseline.resolve()
        spec = importlib.util.spec_from_file_location(path.name.split(".")[0], path)
        baseline = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(baseline)
        functions["Mojo before"] = baseline.loads
    if args.ctypes:
        functions["ctypes"] = ctypes_loads
    for name, data in docs.items():
        expected = orjson.loads(data)
        for label, fn in functions.items():
            if not same(fn(data), expected):
                raise SystemExit(f"{name}: {label} differs from orjson")
    for text in EDGE_CASES:
        outcomes = []
        for fn in (mojo_loads, orjson.loads):
            try:
                outcomes.append(fn(text))
            except ValueError:
                outcomes.append(ValueError)
        if (outcomes[0] is ValueError) != (outcomes[1] is ValueError) or (outcomes[0] is not ValueError and not same(*outcomes)):
            raise SystemExit(f"{text!r}: the Mojo decoder disagrees with orjson")
    print("results identical to orjson on every document and edge case", flush=True)
    print(f"CPython {platform.python_version()}, orjson {orjson.__version__}, {args.pairs} pairs, >= {args.batch_ms:g} ms/batch", flush=True)
    gc.disable()
    print(f"{'document':<22}" + "".join(f"{name:>14}" for name in functions) + "   Mojo/C [p25 - p75]", flush=True)
    results = []
    for name, data in docs.items():
        gc.collect()
        result = measure(data, functions, args.pairs, args.batch_ms / 1000)
        result.update(document=name, bytes=len(data))
        results.append(result)
        cells = "".join(f"{result['seconds'][label] * 1e6:13.1f}µ" for label in functions)
        print(f"{name:<22}{cells}   {result['mojo_over_c']:.3f} [{result['ratio_p25']:.3f} - {result['ratio_p75']:.3f}]", flush=True)
    summary = {
        "corpus_mojo_over_c": st.geometric_mean(r["mojo_over_c"] for r in results[:corpus_count]),
        "corpus_orjson_over_mojo": st.geometric_mean(r["seconds"]["orjson"] / r["seconds"]["Mojo"] for r in results[:corpus_count]),
    }
    if args.baseline:
        summary["corpus_before_over_after"] = st.geometric_mean(r["seconds"]["Mojo before"] / r["seconds"]["Mojo"] for r in results[:corpus_count])
    if args.compact:
        compact_results = results[corpus_count + 1:]
        summary["compact_mojo_over_c"] = st.geometric_mean(r["mojo_over_c"] for r in compact_results)
        if args.baseline:
            summary["compact_before_over_after"] = st.geometric_mean(r["seconds"]["Mojo before"] / r["seconds"]["Mojo"] for r in compact_results)
    print(json.dumps(summary, indent=2), flush=True)
    if args.output:
        report = {"python": platform.python_version(), "platform": platform.platform(),
                  "orjson": orjson.__version__, "yjson_path": yjson.__file__,
                  "mojo_path": yjson.__file__, "c_path": _c_decoder.__file__, "baseline": str(args.baseline) if args.baseline else None,
                  "affinity": sorted(os.sched_getaffinity(0)), "pairs": args.pairs,
                  "pythonhashseed": os.environ.get("PYTHONHASHSEED", "random"),
                  "batch_ms": args.batch_ms, "corpus": str(args.corpus.resolve()),
                  "compact": args.compact,
                  "summary": summary, "results": results}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

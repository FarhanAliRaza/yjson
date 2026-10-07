"""The Mojo decoder against the C one: identical results on the corpus, then alternating timings.

    python bench/mojo_decoder/compare.py [build/jsonexamples] [--pairs N]

Loads loads.so from this directory (build it with build.sh) through ctypes, checks that it
decodes every corpus document and a set of edge cases exactly as orjson does, then times
it against yjson.loads (the C decoder) and orjson.loads in alternating batches.
"""
import argparse
import ctypes
import gc
import math
import statistics as st
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "build"))
import orjson
import yjson

lib = ctypes.PyDLL(str(HERE / "loads.so"))  # PyDLL: the decoder needs the GIL held
lib.yjson_mojo_init.restype = ctypes.c_void_p
lib.yjson_mojo_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
lib.yjson_mojo_loads.restype = ctypes.c_void_p
lib.yjson_mojo_loads.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p]
context = lib.yjson_mojo_init(id(True), id(False), id(None))
error = (ctypes.c_ssize_t * 2)()
error_address = ctypes.addressof(error)
ctypes.pythonapi.Py_DecRef.argtypes = [ctypes.c_void_p]


def mojo_loads(data):
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


EDGE_CASES = ["", " ", "[", "[1,]", "{}", "[]", '""', "1e400", '"\\ud800"', b"\xef\xbb\xbf{}", "[" * 1025 + "]" * 1025, "tru",
              "truee", '{"a":}', "01", "-", "1.", "[1 2]", b'"\xff"', "nul", "nulll", '{"caf\\u00e9": [null, true, false, 1.5e3, -0]}',
              "[18446744073709551615, 18446744073709551616, -9223372036854775808, 0.1234567890123456789, 1.00000000000000000000]"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="?", type=Path, default=ROOT / "build" / "jsonexamples")
    parser.add_argument("--pairs", type=int, default=12)
    args = parser.parse_args()
    sys.setrecursionlimit(20000)
    docs = {p.name: p.read_bytes() for p in sorted(args.corpus.glob("*.json"))}
    record = {"id": 1, "name": "Alice Example", "email": "alice@example.com", "active": True, "score": 97.5,
              "tags": ["admin", "staff"], "address": {"city": "Lahore", "zip": "54000"}, "balance": 1234567, "note": None}
    docs["1000 records"] = orjson.dumps([dict(record, id=i) for i in range(1000)])
    for name, data in docs.items():
        if not same(mojo_loads(data), orjson.loads(data)):
            raise SystemExit(f"{name}: the Mojo decoder's result differs from orjson's")
    for text in EDGE_CASES:
        outcomes = []
        for fn in (mojo_loads, orjson.loads):
            try:
                outcomes.append(fn(text))
            except ValueError:
                outcomes.append(ValueError)
        if (outcomes[0] is ValueError) != (outcomes[1] is ValueError) or (outcomes[0] is not ValueError and not same(*outcomes)):
            raise SystemExit(f"{text!r}: the Mojo decoder disagrees with orjson")
    print("results identical to orjson on every document and edge case")
    gc.disable()
    print(f"{'document':<22}{'C':>10}{'Mojo':>10}{'orjson':>10}   Mojo/C")
    ratios = []
    for name, data in docs.items():
        functions = {"C": yjson.loads, "Mojo": mojo_loads, "orjson": orjson.loads}
        number = 1
        while True:
            start = time.perf_counter()
            for _ in range(number):
                yjson.loads(data)
            if time.perf_counter() - start >= 0.02:
                break
            number *= 2
        samples = {k: [] for k in functions}
        names = list(functions)
        for i in range(args.pairs):
            for k in (names if i % 2 == 0 else names[::-1]):
                start = time.perf_counter()
                for _ in range(number):
                    functions[k](data)
                samples[k].append((time.perf_counter() - start) / number)
        medians = {k: st.median(v) for k, v in samples.items()}
        ratios.append(medians["Mojo"] / medians["C"])
        print(f"{name:<22}{medians['C'] * 1e6:9.1f}µ{medians['Mojo'] * 1e6:9.1f}µ{medians['orjson'] * 1e6:9.1f}µ   {ratios[-1]:5.2f}")
    print(f"geometric mean Mojo/C: {math.exp(sum(map(math.log, ratios)) / len(ratios)):.3f}")


if __name__ == "__main__":
    main()

"""Sustained identical payloads for native sampling profiles."""

import argparse
import contextlib
import os
import time

import mojson

parser = argparse.ArgumentParser()
parser.add_argument("--seconds", type=float, default=10)
parser.add_argument("--option", type=int, default=2564)
parser.add_argument("--backend", choices=("mojson", "orjson"), default="mojson")
parser.add_argument("--cpu", type=int, default=2)
parser.add_argument("--workload", choices=("plain", "reflex-special"), default="plain")
args = parser.parse_args()
os.sched_setaffinity(0, {args.cpu})
if args.backend == "orjson":
    import orjson
    dumps = orjson.dumps
else:
    dumps = mojson.dumps
obj = {"rows": [{"id": i, "name": f"customer_{i}",
    "email": f"user{i}@example.com", "balance": i * 1.37,
    "active": i % 2 == 0, "notes": "lorem ipsum dolor sit amet " * 3}
    for i in range(500)]}
kwargs = {"option": args.option}
context = contextlib.nullcontext()
if args.workload == "reflex-special":
    from reflex.app import _sio_dumps
    from reflex.state import StateUpdate
    from reflex_codec import backend
    context = backend(args.backend)
    dumps = _sio_dumps
    kwargs = {}
    obj = ["event", StateUpdate(delta={"state": {
        "nan": float("nan"), "inf": float("inf"), "neg_inf": -float("inf"),
        "none": None, "text": "café 😀 \\\"\n", "literal": "__reflex_nan__",
        "escape": "__reflex_esc__x", "big": 2**100}})]
with context:
    deadline = time.perf_counter() + args.seconds
    calls = 0
    while time.perf_counter() < deadline:
        dumps(obj, **kwargs)
        calls += 1
print(f"{calls} calls / {args.seconds}s; backend={args.backend}, workload={args.workload}, "
      f"mode={'socket' if args.workload == 'reflex-special' else args.option}")

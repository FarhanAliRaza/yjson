"""Compare dump codecs on pinned Reflex PR 6116 and its real event processor.

Run from the PR checkout with its uv environment and the bench/build paths on
PYTHONPATH. Incoming packets use the PR's actual stdlib Socket.IO decoder.
--all-events adds every event workload of the PR's tests/benchmarks/
test_event_processing.py (cold, warm, bursts, counter and table batches,
router_data), driven through the real processor with wire encoding.
"""

import argparse
import asyncio
import gc
import hashlib
import json
import os
import platform
import statistics
import time
import timeit
from pathlib import Path

import mojson
import orjson
import reflex
from reflex.app import _sio_dumps, _sio_loads
from reflex.state import StateUpdate
from reflex_base.event import Event
from reflex_base.utils import format
from tests.benchmarks.test_event_processing import (
    ROUTER_DATA,
    TOKEN,
    WireBenchState,
    _make_rows,
    _processing_pipeline,
)

from reflex_codec import backend


def paired(name, run, challenger, pairs, batch_ms, payload_bytes):
    """Measure alternating batches and retain every ratio and elapsed time."""
    codecs = ("orjson", challenger)
    for codec in codecs:
        with backend(codec):
            for _ in range(10):
                run()
    number = 1
    while True:
        elapsed = []
        for codec in codecs:
            with backend(codec):
                start = time.perf_counter()
                for _ in range(number):
                    run()
                elapsed.append(time.perf_counter() - start)
        if min(elapsed) >= batch_ms / 1000:
            break
        number *= 2
    samples = []
    for pair in range(pairs):
        timings = {}
        for codec in (codecs if pair % 2 == 0 else codecs[::-1]):
            with backend(codec):
                start = time.perf_counter()
                for _ in range(number):
                    run()
                timings[codec] = (time.perf_counter() - start) / number
        samples.append({**timings, "ratio": timings["orjson"] / timings[challenger]})
    ratios = [sample["ratio"] for sample in samples]
    quartiles = statistics.quantiles(ratios, n=4)
    result = {
        "name": name,
        "challenger": challenger,
        "bytes_per_packet": payload_bytes,
        "iterations_per_batch": number,
        "orjson_us": statistics.median(s["orjson"] for s in samples) * 1e6,
        "challenger_us": statistics.median(s[challenger] for s in samples) * 1e6,
        "ratio": statistics.median(ratios),
        "p25": quartiles[0],
        "p75": quartiles[2],
        "samples": samples,
    }
    print(f"{name:27} {challenger:7} {result['orjson_us']:9.2f} "
          f"{result['challenger_us']:9.2f} us {result['ratio']:5.2f}x "
          f"[{quartiles[0]:.2f}, {quartiles[2]:.2f}]", flush=True)
    return result


def validate_encode(obj, encoder):
    """Check exact output identity before timing either native backend."""
    outputs = {}
    for codec in ("orjson", "mojson"):
        with backend(codec):
            outputs[codec] = encoder(obj)
    assert outputs["mojson"] == outputs["orjson"], outputs
    return len(outputs["orjson"].encode())


def measure_wire(row_count, args):
    """Drive three incoming events through real state, callbacks, and codecs."""
    loop = asyncio.new_event_loop()
    wire = []

    async def emit(token, delta):
        wire.append(_sio_dumps(["event", StateUpdate(delta=delta)], separators=(",", ":")))

    pipeline = _processing_pipeline(emit)
    processor = loop.run_until_complete(pipeline.__aenter__())
    handler = format.format_event_handler(WireBenchState.event_handlers["refresh_rows"])
    raw = json.dumps({"name": handler, "router_data": ROUTER_DATA,
                      "payload": {"count": row_count}})

    async def event_batch():
        wire.clear()
        async with processor as p:
            futures = []
            for _ in range(3):
                fields = _sio_loads(raw)
                futures.append(await p.enqueue(TOKEN, Event(**fields)))
            for future in asyncio.as_completed(futures):
                await future
        assert len(wire) == 3

    def run():
        loop.run_until_complete(event_batch())

    try:
        expected = None
        for codec in ("orjson", "mojson"):
            with backend(codec):
                run()
                if expected is None:
                    expected = wire.copy()
                else:
                    assert expected == wire
        results = []
        for challenger in ("mojson", "stdlib"):
            results.append(paired(f"wire 3 events / {row_count} rows", run, challenger,
                                  args.pairs, args.batch_ms, len(wire[0].encode())))
        return results
    finally:
        loop.run_until_complete(pipeline.__aexit__(None, None, None))
        loop.close()


def measure_events(args):
    """Every event workload of the PR's benchmark module, with wire encoding.

    Each workload runs through the real BaseStateEventProcessor and
    StateManagerMemory exactly as tests/benchmarks/test_event_processing.py
    does (cold and warm single events, bursts on one and on independent
    tokens, the counter and table batches, and router_data preparation), and
    every emitted delta is encoded with the app's _sio_dumps so the codec is
    on the measured path. Wire output is checked equal across codecs first.
    """
    from reflex.istate.manager.token import BaseStateToken
    from tests.benchmarks.fixtures import BenchmarkState, TableState
    from tests.benchmarks.test_event_processing import TABLE_STATUSES, _events

    loop = asyncio.new_event_loop()
    wire = []
    deltas = []

    async def emit(token, delta):
        deltas.append(delta)
        wire.append(_sio_dumps(["event", StateUpdate(delta=delta)], separators=(",", ":")))

    pipeline = _processing_pipeline(emit)
    processor = loop.run_until_complete(pipeline.__aenter__())
    state_manager = processor._root_context.state_manager
    increment = format.format_event_handler(BenchmarkState.event_handlers["increment"])
    decrement = format.format_event_handler(BenchmarkState.event_handlers["decrement"])
    set_status = format.format_event_handler(TableState.event_handlers["set_status"])

    async def process(pairs):
        wire.clear()
        deltas.clear()
        async with processor as p:
            futures = [await p.enqueue(token, event) for token, event in pairs]
            for future in asyncio.as_completed(futures):
                await future
        assert len(wire) == len(pairs), (len(wire), len(pairs))

    def run_batch(pairs):
        loop.run_until_complete(process(pairs))

    def burst(token_names):
        return [(token, _events(increment, [{}])[0]) for token in token_names]

    # BenchmarkState.nested_elements is O(counter^2) per delta, so a token's
    # counter must return to its initial value after each run or the workload
    # grows without bound across the paired repetitions (CodSpeed measures a
    # single round, where the counter stays at its initial value).
    substate_path = BenchmarkState.get_full_name().split(".")
    initial_counter = BenchmarkState.get_root_state()(_reflex_internal_init=True).get_substate(substate_path).counter

    def stationary(pairs):
        tokens = {token for token, _ in pairs}

        def run():
            run_batch(pairs)
            for token in tokens:
                substate = state_manager.states[token].get_substate(substate_path)
                substate.counter = initial_counter
                substate._clean()

        return run

    cold_counter = [0]

    def run_cold():
        cold_counter[0] += 1
        token = f"cold-token-{cold_counter[0]}"
        run_batch(burst([token]))
        state_manager._purge_token(BaseStateToken(ident=token, cls=BenchmarkState))

    workloads = [("event cold / 1 event", run_cold)]
    warm = burst(["warm-token"])
    stationary(warm)()
    workloads.append(("event warm / 1 event", stationary(warm)))
    for count in (10, 100):
        same = burst(["burst-token"] * count)
        stationary(same[:1])()
        workloads.append((f"burst same token / {count} events", stationary(same)))
        independent = burst([f"independent-token-{index}" for index in range(count)])
        stationary(independent)()
        workloads.append((f"burst independent / {count} events", stationary(independent)))
    counter = [(TOKEN, event) for event in _events(increment, [{}] * 2) + _events(decrement, [{}] * 2)]
    workloads.append(("counter batch / 4 events", lambda: run_batch(counter)))
    table = [(TOKEN, event) for event in _events(set_status, [{"status": status} for status in TABLE_STATUSES * 2])]
    run_batch(table)
    workloads.append(("table batch / 6 events", lambda: run_batch(table)))

    results = []
    try:
        for name, run in workloads:
            # State advances between runs (counters), so compare the codecs on
            # one run's captured deltas rather than on two runs' wire output.
            run()
            captured = [["event", StateUpdate(delta=delta)] for delta in deltas]
            size = sum(validate_encode(packet, lambda obj: _sio_dumps(obj, separators=(",", ":")))
                       for packet in captured)
            gc.collect()
            result = paired(name, run, "mojson", args.pairs, args.batch_ms, size)
            # How much of the workload is encoding at all: the time to encode the
            # captured packets with each codec, relative to the orjson workload.
            for codec in ("orjson", "mojson"):
                with backend(codec):
                    encode_us = min(timeit.repeat(lambda: [_sio_dumps(packet, separators=(",", ":")) for packet in captured],
                                                  number=1, repeat=50)) * 1e6
                result[f"encode_{codec}_us"] = encode_us
            result["encode_share"] = result["encode_orjson_us"] / result["orjson_us"]
            print(f"{'':27} encode only: orjson {result['encode_orjson_us']:9.2f} mojson {result['encode_mojson_us']:9.2f} us,"
                  f" {result['encode_share'] * 100:.1f}% of the orjson workload", flush=True)
            results.append(result)
    finally:
        loop.run_until_complete(pipeline.__aexit__(None, None, None))
        loop.close()

    # on_event router_data preparation: enqueue is mocked, no encoding involved (control row)
    from unittest import mock
    from reflex.app import App, EventNamespace
    app = App()
    app._event_processor = mock.Mock(enqueue=mock.AsyncMock())
    namespace = EventNamespace("/event", app)
    sid = "benchmark-sid"
    environ = {"QUERY_STRING": "token=benchmark-token", "asgi.scope": {"headers": [
        (b"host", b"localhost:3000"), (b"origin", b"http://localhost:3000"),
        (b"user-agent", b"Mozilla/5.0 (X11; Linux x86_64) benchmark"), (b"cookie", b"session=abc123"),
        (b"x-forwarded-for", b"203.0.113.7, 10.0.0.1")], "client": ("127.0.0.1", 54321)}}
    message = {"name": "state.hydrate", "router_data": {"pathname": "/", "query": {}, "asPath": "/"}, "payload": {}}

    async def on_events():
        for _ in range(10):
            await namespace.on_event(sid, message)

    loop = asyncio.new_event_loop()
    loop.run_until_complete(namespace.on_connect(sid, environ))
    try:
        results.append(paired("on_event router_data / 10 events (control)", lambda: loop.run_until_complete(on_events()),
                              "mojson", args.pairs, args.batch_ms, 0))
    finally:
        loop.close()
    return results


def main():
    """Validate and benchmark the PR's encoders on a single pinned CPU."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pairs", type=int, default=40)
    parser.add_argument("--batch-ms", type=float, default=10)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--options-only", action="store_true",
                        help="Isolate flags on one 500-row plain dictionary")
    parser.add_argument("--case", action="append", default=[],
                        help="Run only case names containing this substring (repeatable)")
    parser.add_argument("--all-events", action="store_true",
                        help="Also run every event workload of the PR's benchmark module through the processor with wire encoding")
    args = parser.parse_args()
    os.sched_setaffinity(0, {args.cpu})
    results = []
    print(f"Python {platform.python_version()}, orjson {orjson.__version__}, "
          f"Reflex {reflex.__file__}, {args.pairs} alternating pairs", flush=True)
    cases = []
    for rows in (5, 500):
        obj = ["event", StateUpdate(delta={WireBenchState.get_full_name():
                                          {"rows": _make_rows(rows)}})]
        cases.append((f"socket encode / {rows} rows", obj, _sio_dumps))
    cases.extend([
        ("socket None / 500 rows", ["event", StateUpdate(delta={"state":
            {"rows": _make_rows(500), "optional": None}})], _sio_dumps),
        ("socket special values", ["event", StateUpdate(delta={"state": {
            "nan": float("nan"), "inf": float("inf"), "neg_inf": -float("inf"),
            "none": None, "text": "café 😀 \\\"\n", "literal": "__reflex_nan__",
            "escape": "__reflex_esc__x", "big": 2**100}})], _sio_dumps),
        ("artifact compact", {"rows": _make_rows(500)}, format.orjson_dumps),
        ("artifact indent", {"rows": _make_rows(500)},
         lambda obj: format.orjson_dumps(obj, indent=2)),
        ("artifact sorted", {"rows": _make_rows(500)},
         lambda obj: format.orjson_dumps(obj, sort_keys=True)),
    ])
    if args.options_only:
        obj = {"rows": _make_rows(500)}
        cases = []
        for name, flags in (
            ("native no flags", 0),
            ("native passthrough", format._ORJSON_PASSTHROUGH_OPTS),
            ("native socket flags", format._ORJSON_SOCKET_OPTS),
        ):
            cases.append((name, obj, lambda obj, flags=flags:
                format.orjson.dumps(obj, default=format._orjson_default,
                                    option=flags).decode()))
    for name, obj, encoder in cases:
        if args.case and not any(part in name for part in args.case):
            continue
        size = validate_encode(obj, encoder)
        for challenger in (("mojson",) if args.options_only else ("mojson", "stdlib")):
            gc.collect()
            results.append(paired(name, lambda: encoder(obj), challenger,
                                  args.pairs, args.batch_ms, size))
    if not args.options_only:
        for count in (5, 500):
            if args.case and not any(part in f"wire 3 events / {count} rows" for part in args.case):
                continue
            results.extend(measure_wire(count, args))
        if args.all_events:
            results.extend(measure_events(args))
    build = Path(mojson.__file__)
    report = {
        "reflex_commit": "f2b00b407b892778e259eb0c671a89c03524ea7d",
        "python": platform.python_version(), "orjson": orjson.__version__,
        "reflex_source": reflex.__file__, "mojson_binary": str(build),
        "mojson_sha256": hashlib.sha256(build.read_bytes()).hexdigest(),
        "cpu": args.cpu, "pairs": args.pairs, "gc_enabled": gc.isenabled(),
        "batch_ms": args.batch_ms,
        "case_filter": args.case,
        "decoding": "PR _sio_loads (stdlib), unchanged for both dump backends",
        "adapter": "mojson.dumps for artifacts; native mojson.dumps_socket replaces the socket retry path; orjson retains the untouched PR path",
        "results": results,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

"""Decoding into objects: loads(type=...) against msgspec's typed decoders and building by hand.

    python bench/bench_typed.py [--pairs 40]

The payload is 1,000 records of nine fields with one nested object. Every decoder's result
is checked to hold the same values before timing. Values above 1 in the last column mean
yjson is faster than that row.
"""
import argparse
import dataclasses
import gc
import math
from pathlib import Path
import statistics as st
import sys
import timeit
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import yjson
import orjson

try:
    import msgspec
except ImportError:
    msgspec = None


@dataclasses.dataclass
class Address:
    city: str
    zip: str


@dataclasses.dataclass
class Record:
    id: int
    name: str
    email: str
    active: bool
    score: float
    tags: list[str]
    address: Address
    balance: int
    note: Optional[str]


@dataclasses.dataclass(slots=True)
class SlotAddress:
    city: str
    zip: str


@dataclasses.dataclass(slots=True)
class SlotRecord:
    id: int
    name: str
    email: str
    active: bool
    score: float
    tags: list[str]
    address: SlotAddress
    balance: int
    note: Optional[str]


def by_hand(data):
    """What code without typed decoding does: parse, then build the objects."""
    return [Record(address=Address(**item.pop("address")), **item) for item in orjson.loads(data)]


def as_plain(value):
    """Objects back to dicts, so every decoder's result compares with the untyped one."""
    if dataclasses.is_dataclass(value):
        return {f.name: as_plain(getattr(value, f.name)) for f in dataclasses.fields(value)}
    if msgspec is not None and isinstance(value, msgspec.Struct):
        return {f: as_plain(getattr(value, f)) for f in value.__struct_fields__}
    if isinstance(value, list):
        return [as_plain(v) for v in value]
    if isinstance(value, dict):
        return {k: as_plain(v) for k, v in value.items()}
    return value


def measure(data, functions, pairs, batch_seconds):
    timers = {name: timeit.Timer(lambda fn=fn: fn(data)) for name, fn in functions.items()}
    for timer in timers.values():
        timer.timeit(3)
    number = 1
    while min(timer.timeit(number) for timer in timers.values()) < batch_seconds:
        number *= 2
    samples = {name: [] for name in functions}
    names = list(functions)
    for pair in range(pairs):
        for name in (names if pair % 2 == 0 else names[::-1]):
            samples[name].append(timers[name].timeit(number) / number)
    return {name: st.median(times) for name, times in samples.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pairs", type=int, default=40)
    parser.add_argument("--batch-seconds", type=float, default=0.01)
    args = parser.parse_args()
    record = {"id": 1, "name": "Alice Example", "email": "alice@example.com", "active": True, "score": 97.5,
              "tags": ["admin", "staff"], "address": {"city": "Lahore", "zip": "54000"}, "balance": 1234567, "note": None}
    data = orjson.dumps([dict(record, id=i) for i in range(1000)])
    functions = {
        "yjson.loads -> dicts": yjson.loads,
        "orjson.loads -> dicts": orjson.loads,
        "orjson + dataclasses by hand": by_hand,
        "yjson type=list[Record]": lambda d: yjson.loads(d, type=list[Record]),
        "yjson type=list[SlotRecord]": lambda d: yjson.loads(d, type=list[SlotRecord]),
    }
    if msgspec is not None:
        class MsAddress(msgspec.Struct):
            city: str
            zip: str

        class MsRecord(msgspec.Struct):
            id: int
            name: str
            email: str
            active: bool
            score: float
            tags: list[str]
            address: MsAddress
            balance: int
            note: Optional[str]

        functions["msgspec -> dicts"] = msgspec.json.Decoder().decode
        functions["msgspec -> list[Record] dataclass"] = msgspec.json.Decoder(list[Record]).decode
        functions["msgspec -> list[Struct]"] = msgspec.json.Decoder(list[MsRecord]).decode
    expected = as_plain(yjson.loads(data))
    for name, fn in functions.items():
        if as_plain(fn(data)) != expected:
            raise ValueError(f"{name} produced different values")
    gc.disable()
    medians = measure(data, functions, args.pairs, args.batch_seconds)
    reference = medians["yjson type=list[Record]"]
    print(f"{'decoder':<36} {'per call':>10}   vs yjson type=list[Record]")
    for name, seconds in medians.items():
        print(f"{name:<36} {seconds * 1e6:8.1f} µs   {seconds / reference:5.2f}×")


if __name__ == "__main__":
    main()

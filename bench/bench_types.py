"""Per-item cost of the non-container types (datetime, UUID, dataclass, Enum, ...) versus orjson.

Paired alternating runs on one core; prints nanoseconds per item for lists of 100.
"""
import dataclasses
import datetime
import enum
import os
from pathlib import Path
import statistics as st
import sys
import timeit
import uuid
import zoneinfo

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import yjson
import orjson


@dataclasses.dataclass
class Record:
    id: int
    name: str


@dataclasses.dataclass(slots=True)
class SlotRecord:
    id: int
    name: str


@dataclasses.dataclass
class PrivateRecord:
    id: int
    _hidden: str = "x"


class Choice(enum.Enum):
    A = "a"


class Code(enum.IntEnum):
    OK = 200


def main():
    os.sched_setaffinity(0, {2})
    utc = datetime.timezone.utc
    amsterdam = zoneinfo.ZoneInfo("Europe/Amsterdam")
    cases = {
        "datetime naive": [datetime.datetime(2024, 1, 2, 3, 4, 5, i) for i in range(100)],
        "datetime utc": [datetime.datetime(2024, 1, 2, 3, 4, 5, i, tzinfo=utc) for i in range(100)],
        "datetime zoneinfo": [datetime.datetime(2024, 1, 2, 3, 4, 5, i, tzinfo=amsterdam) for i in range(100)],
        "date": [datetime.date(2024, 1, 1 + i % 28) for i in range(100)],
        "time": [datetime.time(1, 2, 3, i) for i in range(100)],
        "UUID": [uuid.UUID(int=i * 7919) for i in range(100)],
        "dataclass": [Record(i, "Ada") for i in range(100)],
        "dataclass slots": [SlotRecord(i, "Ada") for i in range(100)],
        "dataclass private": [PrivateRecord(i) for i in range(100)],
        "Enum": [Choice.A] * 100,
        "IntEnum": [Code.OK] * 100,
        "dict of datetimes": [{"t": datetime.datetime(2024, 1, 2, 3, 4, 5, i), "u": uuid.UUID(int=i)} for i in range(100)],
        "default callback": ([object() for _ in range(100)], lambda o: "custom"),
    }
    print(f"{'case (ns per item)':24}{'orjson':>8}{'yjson':>8}{'ratio':>8}", flush=True)
    for name, case in cases.items():
        value, default = case if isinstance(case, tuple) else (case, None)
        count = len(value)
        assert yjson.dumps(value, default=default) == orjson.dumps(value, default=default), name
        timers = [timeit.Timer(lambda: orjson.dumps(value, default=default)), timeit.Timer(lambda: yjson.dumps(value, default=default))]
        for timer in timers:
            timer.timeit(100)
        number = 2000
        samples = [[], []]
        for pair in range(20):
            for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
                samples[index].append(timers[index].timeit(number) / number / count * 1e9)
        a, b = st.median(samples[0]), st.median(samples[1])
        print(f"{name:24}{a:8.1f}{b:8.1f}{a / b:7.2f}x", flush=True)


if __name__ == "__main__":
    main()

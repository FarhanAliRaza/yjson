"""Paired measurements of the enabled feature paths versus orjson."""
import dataclasses
import datetime
import enum
import json
import os
from pathlib import Path
import statistics as st
import sys
import timeit
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import mojson
import orjson


@dataclasses.dataclass
class Record:
    id: int
    name: str


class Choice(enum.Enum):
    A = "a"


def main():
    os.sched_setaffinity(0, {2})
    obj = [{"z": i, "a": [0.1, "value", None]} for i in range(100)]
    dates = [datetime.datetime(2024, 1, 2, 3, 4, 5, i) for i in range(100)]
    markers = [object() for _ in range(100)]
    cases = [("compact", obj, 0, None), ("indent", obj, 1, None), ("sorted", obj, 32, None),
             ("indent + sorted", obj, 33, None), ("strict integers", obj, 64, None),
             ("non-str keys", [{i: "value"} for i in range(100)], 4, None),
             ("datetime", dates, 0, None), ("UUID", [uuid.UUID(int=i) for i in range(100)], 0, None),
             ("dataclass", [Record(i, "Ada") for i in range(100)], 0, None),
             ("Enum", [Choice.A] * 100, 0, None), ("default callback", markers, 0, lambda o: "custom")]
    results = []
    print(f"{'feature (100 items)':22} {'orjson us':>10} {'mojson us':>10} {'ratio':>8}", flush=True)
    for name, value, option, default in cases:
        assert mojson.dumps(value, option=option, default=default) == orjson.dumps(value, option=option, default=default), name
        timers = [timeit.Timer(lambda: orjson.dumps(value, option=option, default=default)),
                  timeit.Timer(lambda: mojson.dumps(value, option=option, default=default))]
        for timer in timers:
            timer.timeit(10)
        number = 1
        while min(timer.timeit(number) for timer in timers) < 0.01:
            number *= 2
        samples = []
        for pair in range(40):
            times = [0, 0]
            for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
                times[index] = timers[index].timeit(number) / number
            samples.append({"orjson": times[0], "mojson": times[1], "ratio": times[0] / times[1]})
        a, b = [st.median(s[key] for s in samples) * 1e6 for key in ("orjson", "mojson")]
        ratio = st.median(s["ratio"] for s in samples)
        print(f"{name:22} {a:10.3f} {b:10.3f} {ratio:7.3f}x", flush=True)
        results.append({"name": name, "orjson_us": a, "mojson_us": b, "ratio": ratio, "samples": samples})
    Path("build/features-benchmark.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()

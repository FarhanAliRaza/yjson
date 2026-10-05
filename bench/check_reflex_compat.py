"""Check wire semantics and document byte differences beyond the PR suite."""

import datetime
import json
import math
from pathlib import Path

from reflex.app import _sio_loads
from reflex.state import StateUpdate
from reflex_base.utils import format

from reflex_codec import backend


def normalized(value):
    """Name non-finite floats so decoded outputs compare by value."""
    if isinstance(value, float) and not math.isfinite(value):
        return {"non_finite": repr(value)}
    if isinstance(value, list):
        return [normalized(item) for item in value]
    if isinstance(value, dict):
        return {key: normalized(item) for key, item in value.items()}
    return value


def main():
    """Validate serializer callbacks, fallback cases, and incoming big integers."""
    cases = {
        "nan and scientific float": {"nan": float("nan"), "tiny": 1e-7},
        "infinity and small floats": {"inf": float("inf"), "nums": [1e-5, 1e-6, 1e-7]},
        "None and scientific float": {"none": None, "tiny": 1e-7},
        "sentinels and scientific float": {"s": "__reflex_nan__", "tiny": 1e-7},
        "integer keys": {1: "x", False: "y", None: "z"},
        "dates and big integer": {"date": datetime.datetime(2026, 10, 6, 1, 2, 3),
                                  "big": 2**100},
        "surrogate": {"value": "path\udcff"},
        "registered StateUpdate": ["event", StateUpdate(delta={"state": {
            "text": "café 😀 \\\"\n", "nan": float("nan")}})],
    }
    results = []
    for name, obj in cases.items():
        outputs = {}
        for codec in ("orjson", "mojson"):
            with backend(codec):
                outputs[codec] = format.orjson_dumps_socket(obj)
                assert _sio_loads('{"id":1267650600228229401496703205376}')["id"] == 2**100
        same_semantics = normalized(json.loads(outputs["orjson"])) == normalized(json.loads(outputs["mojson"]))
        assert same_semantics, name
        results.append({"name": name, "same_semantics": same_semantics,
                        "same_bytes": outputs["orjson"] == outputs["mojson"],
                        "outputs": outputs})
    Path("../reflex-compat.json").write_text(json.dumps(results, indent=2, ensure_ascii=True) + "\n")
    print(f"{len(results)} wire semantic checks passed; "
          f"{sum(r['same_bytes'] for r in results)} have identical bytes")
    for result in results:
        if not result["same_bytes"]:
            print(result["name"], result["outputs"])


if __name__ == "__main__":
    main()

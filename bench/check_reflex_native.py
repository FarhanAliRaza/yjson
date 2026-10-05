"""Verify native wire semantics, including with every stdlib retry disabled."""

import json
import random
from pathlib import Path
from unittest.mock import patch
import hashlib
import mojson

from reflex.app import _sio_dumps
from reflex.state import StateUpdate
from reflex_base.utils import format
from reflex_components_core.core import _upload

from check_reflex_compat import normalized
from reflex_codec import backend


def main():
    rng = random.Random(6116)
    scalars = [None, True, False, 0, -2**100, 2**100, 0.1, 1e-7,
               float("nan"), float("inf"), -float("inf"), "__reflex_nan__",
               "__reflex_inf__", "__reflex_neg_inf__", "__reflex_esc__x",
               "__reflex_custom__", '😀 café " \\ \n', "path\udcff"]

    def value(depth=0):
        if depth > 3 or rng.random() < 0.5:
            return rng.choice(scalars)
        if rng.random() < 0.5:
            return [value(depth + 1) for _ in range(rng.randrange(8))]
        return {rng.choice(("x", "null", "__reflex_nan__", "k\ud800", 2**100, None)): value(depth + 1)
                for _ in range(rng.randrange(8))}

    payloads = [["event", StateUpdate(delta={"state": {"value": value()}})] for _ in range(3000)]
    with backend("orjson"):
        expected = [normalized(json.loads(_sio_dumps(obj))) for obj in payloads]
    with backend("mojson"), \
         patch.object(format, "_json_dumps_socket_fallback", side_effect=AssertionError("stdlib retry used")), \
         patch.object(format, "_replace_non_finite_floats", side_effect=AssertionError("Python marker walk used")), \
         patch.object(json, "dumps", side_effect=AssertionError("stdlib encoder used")):
        assert _upload.orjson_dumps_socket is format.orjson_dumps_socket
        assert normalized(json.loads(_upload.orjson_dumps_socket(payloads[0]))) == expected[0]
        for obj, reference in zip(payloads, expected):
            out = _sio_dumps(obj, separators=(",", ":"))
            out.encode("utf-8")
            assert normalized(json.loads(out)) == reference, (out, reference)
    report = {"packets": len(payloads), "same_semantics": True,
              "stdlib_json_dumps_disabled": True, "stdlib_retry_disabled": True,
              "python_marker_walk_disabled": True,
              "upload_writer_uses_native_socket": True,
              "mojson_sha256": hashlib.sha256(Path(mojson.__file__).read_bytes()).hexdigest()}
    Path("../socket-native-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(payloads)} native wire packets passed with stdlib encoding/retry and Python marker walk disabled")


if __name__ == "__main__":
    main()

"""Run the full untyped decoding differential checks on the standalone Mojo adapter.

    .venv-bench/bin/python bench/mojo_decoder/check.py [build/jsonexamples]

Uses tests/check_loads.py unchanged, including fixtures, generated numbers,
float rounding, escaped strings, invalid UTF-8, mutations and input variants.
"""
import importlib.util
from pathlib import Path
import sys

import _mojo_decoder

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_mojo_loads", ROOT / "tests/check_loads.py")
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)
checks.yjson = _mojo_decoder

if __name__ == "__main__":
    sys.exit(checks.main())

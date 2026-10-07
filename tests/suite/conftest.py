"""Run the suite against yjson.

The local build in build/ takes precedence over an installed wheel, as in
tests/check_features.py. Each test runs in a forked child (pytest-forked), so a
test that crashes the interpreter fails on its own instead of ending the run.

The intended differences from orjson are declared below as strict expected
failures: a new difference fails the run, and so does one of these starting
to pass (the list is then stale).
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "build"))

NAN_INFINITY = "yjson writes NaN and Infinity as Python's json does; orjson writes null"
STDLIB_PARSER = "loads() uses the stdlib parser, whose error message and position differ"
INTENDED_DIFFERENCES = {
    "test_type.py::TestType::test_nan_dumps": NAN_INFINITY,
    "test_type.py::TestType::test_infinity_dumps": NAN_INFINITY,
    "test_numpy.py::TestNumpy::test_numpy_array_f16_edge": NAN_INFINITY,
    "test_numpy.py::TestNumpy::test_numpy_array_f32_edge": NAN_INFINITY,
    "test_numpy.py::TestNumpy::test_numpy_array_f64_edge": NAN_INFINITY,
    "test_error.py::TestJsonDecodeError::test_empty": STDLIB_PARSER,
    "test_error.py::TestJsonDecodeError::test_leading_padding": STDLIB_PARSER,
}


def pytest_configure(config):
    if config.pluginmanager.hasplugin("pytest_forked"):
        config.option.forked = True


def pytest_collection_modifyitems(items):
    for item in items:
        reason = INTENDED_DIFFERENCES.get(item.nodeid.split("tests/suite/")[-1].split("suite/")[-1])
        if reason:
            item.add_marker(pytest.mark.xfail(reason=reason, strict=True))

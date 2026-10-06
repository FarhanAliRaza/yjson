"""Run the suite against yjson.

The local build in build/ takes precedence over an installed wheel, as in
tests/check_features.py. Each test runs in a forked child (pytest-forked), so a
test that crashes the interpreter fails on its own instead of ending the run.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "build"))


def pytest_configure(config):
    if config.pluginmanager.hasplugin("pytest_forked"):
        config.option.forked = True

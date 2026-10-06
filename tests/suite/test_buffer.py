# SPDX-License-Identifier: MPL-2.0
# Copyright ijl (2025)

import os

import pytest

import yjson

YJSON_RUNNER_MEMORY_GIB = os.getenv("YJSON_RUNNER_MEMORY_GIB", "")


@pytest.mark.skipif(
    not YJSON_RUNNER_MEMORY_GIB,
    reason="YJSON_RUNNER_MEMORY_GIB not defined",
)
def test_memory_loads():
    buffer_factor = 12
    segment = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    size = (
        (int(YJSON_RUNNER_MEMORY_GIB) * 1024 * 1024 * 1024)
        // buffer_factor
        // len(segment)
    )
    doc = "".join(segment for _ in range(size))
    with pytest.raises(yjson.JSONDecodeError) as exc_info:
        _ = yjson.loads(doc)
    assert (
        str(exc_info.value)
        == "Not enough memory to allocate buffer for parsing: line 1 column 1 (char 0)"
    )

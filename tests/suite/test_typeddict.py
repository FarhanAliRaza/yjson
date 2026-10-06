# SPDX-License-Identifier: MPL-2.0
# Copyright ijl (2019-2023)

import yjson

try:
    from typing import TypedDict  # type: ignore
except ImportError:
    from typing_extensions import TypedDict


class TestTypedDict:
    def test_typeddict(self):
        """
        dumps() TypedDict
        """

        class TypedDict1(TypedDict):
            a: str
            b: int

        obj = TypedDict1(a="a", b=1)
        assert yjson.dumps(obj) == b'{"a":"a","b":1}'

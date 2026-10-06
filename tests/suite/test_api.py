# SPDX-License-Identifier: (Apache-2.0 OR MIT)
# Copyright ijl (2018-2026), hauntsaninja (2020)

import datetime
import inspect
import json
import re

import pytest

import yjson

SIMPLE_TYPES = (1, 1.0, -1, None, "str", True, False)

LOADS_RECURSION_LIMIT = 1024


def default(obj):
    return str(obj)


class TestApi:
    def test_loads_trailing(self):
        """
        loads() handles trailing whitespace
        """
        assert yjson.loads("{}\n\t ") == {}

    def test_loads_trailing_invalid(self):
        """
        loads() handles trailing invalid
        """
        pytest.raises(yjson.JSONDecodeError, yjson.loads, "{}\n\t a")

    def test_simple_json(self):
        """
        dumps() equivalent to json on simple types
        """
        for obj in SIMPLE_TYPES:
            assert yjson.dumps(obj) == json.dumps(obj).encode("utf-8")

    def test_simple_round_trip(self):
        """
        dumps(), loads() round trip on simple types
        """
        for obj in SIMPLE_TYPES:
            assert yjson.loads(yjson.dumps(obj)) == obj

    def test_loads_type(self):
        """
        loads() invalid type
        """
        for val in (1, 3.14, [], {}, None):  # type: ignore
            pytest.raises(yjson.JSONDecodeError, yjson.loads, val)  # type: ignore

    def test_loads_recursion_partial(self):
        """
        loads() recursion limit partial
        """
        pytest.raises(yjson.JSONDecodeError, yjson.loads, "[" * (1024 * 1024))

    def test_loads_recursion_valid_limit_array(self):
        """
        loads() recursion limit at limit array
        """
        n = LOADS_RECURSION_LIMIT + 1
        value = b"[" * n + b"]" * n
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_limit_object(self):
        """
        loads() recursion limit at limit object
        """
        n = LOADS_RECURSION_LIMIT
        value = b'{"key":' * n + b'{"key":true}' + b"}" * n
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_limit_mixed(self):
        """
        loads() recursion limit at limit mixed
        """
        n = LOADS_RECURSION_LIMIT
        value = b"".join((b"[", b'{"key":' * n, b'{"key":true}' + b"}" * n, b"]"))
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_excessive_array(self):
        """
        loads() recursion limit excessively high value
        """
        n = 10000000
        value = b"[" * n + b"]" * n
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_limit_array_pretty(self):
        """
        loads() recursion limit at limit array pretty
        """
        n = LOADS_RECURSION_LIMIT + 1
        value = b"[\n  " * n + b"]" * n
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_limit_object_pretty(self):
        """
        loads() recursion limit at limit object pretty
        """
        n = LOADS_RECURSION_LIMIT
        value = b'{\n  "key":' * n + b'{"key":true}' + b"}" * n
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_limit_mixed_pretty(self):
        """
        loads() recursion limit at limit mixed pretty
        """
        n = LOADS_RECURSION_LIMIT
        value = b'[\n  {"key":' * n + b'{"key":true}' + b"}" * n + b"]"
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_loads_recursion_valid_excessive_array_pretty(self):
        """
        loads() recursion limit excessively high value pretty
        """
        n = 10000000
        value = b"[\n  " * n + b"]" * n
        pytest.raises(yjson.JSONDecodeError, yjson.loads, value)

    def test_version(self):
        """
        __version__
        """
        assert re.match(r"^\d+\.\d+(\.\d+)?$", yjson.__version__)

    def test_valueerror(self):
        """
        yjson.JSONDecodeError is a subclass of ValueError
        """
        pytest.raises(yjson.JSONDecodeError, yjson.loads, "{")
        pytest.raises(ValueError, yjson.loads, "{")

    def test_optional_none(self):
        """
        dumps() option, default None
        """
        assert yjson.dumps([], option=None) == b"[]"
        assert yjson.dumps([], default=None) == b"[]"
        assert yjson.dumps([], option=None, default=None) == b"[]"
        assert yjson.dumps([], None, None) == b"[]"

    def test_option_not_int(self):
        """
        dumps() option not int or None
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(True, option=True)

    def test_option_invalid_int(self):
        """
        dumps() option invalid 64-bit number
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(True, option=9223372036854775809)

    def test_option_range_low(self):
        """
        dumps() option out of range low
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(True, option=-1)

    def test_option_range_high(self):
        """
        dumps() option out of range high
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(True, option=1 << 12)

    def test_opts_multiple(self):
        """
        dumps() multiple option
        """
        assert (
            yjson.dumps(
                [1, datetime.datetime(2000, 1, 1, 2, 3, 4)],
                option=yjson.OPT_STRICT_INTEGER | yjson.OPT_NAIVE_UTC,
            )
            == b'[1,"2000-01-01T02:03:04+00:00"]'
        )

    def test_default_positional(self):
        """
        dumps() positional arg
        """
        with pytest.raises(TypeError):
            yjson.dumps(__obj={})  # type: ignore
        with pytest.raises(TypeError):
            yjson.dumps(zxc={})  # type: ignore

    def test_default_unknown_kwarg(self):
        """
        dumps() unknown kwarg
        """
        with pytest.raises(TypeError):
            yjson.dumps({}, zxc=default)  # type: ignore

    def test_default_empty_kwarg(self):
        """
        dumps() empty kwarg
        """
        assert yjson.dumps(None) == b"null"

    def test_default_twice(self):
        """
        dumps() default twice
        """
        with pytest.raises(TypeError):
            yjson.dumps({}, default, default=default)  # type: ignore

    def test_option_twice(self):
        """
        dumps() option twice
        """
        with pytest.raises(TypeError):
            yjson.dumps({}, None, yjson.OPT_NAIVE_UTC, option=yjson.OPT_NAIVE_UTC)  # type: ignore

    def test_option_mixed(self):
        """
        dumps() option one arg, one kwarg
        """

        class Custom:
            def __str__(self):
                return "zxc"

        assert (
            yjson.dumps(
                [Custom(), datetime.datetime(2000, 1, 1, 2, 3, 4)],
                default,
                option=yjson.OPT_NAIVE_UTC,
            )
            == b'["zxc","2000-01-01T02:03:04+00:00"]'
        )

    def test_dumps_signature(self):
        """
        dumps() valid __text_signature__
        """
        assert (
            str(inspect.signature(yjson.dumps))
            == "(obj, /, default=None, option=None)"
        )
        inspect.signature(yjson.dumps).bind("str")
        inspect.signature(yjson.dumps).bind("str", default=default, option=1)
        inspect.signature(yjson.dumps).bind("str", default=None, option=None)

    def test_loads_signature(self):
        """
        loads() valid __text_signature__
        """
        assert str(inspect.signature(yjson.loads)), "(obj == /)"
        inspect.signature(yjson.loads).bind("[]")

    def test_dumps_module_str(self):
        """
        yjson.dumps.__module__ is a str
        """
        assert yjson.dumps.__module__ == "yjson"

    def test_loads_module_str(self):
        """
        yjson.loads.__module__ is a str
        """
        assert yjson.loads.__module__ == "yjson"

    def test_bytes_buffer(self):
        """
        dumps() trigger buffer growing where length is greater than growth
        """
        a = "a" * 900
        b = "b" * 4096
        c = "c" * 4096 * 4096
        assert yjson.dumps([a, b, c]) == f'["{a}","{b}","{c}"]'.encode("utf-8")

    def test_bytes_null_terminated(self):
        """
        dumps() PyBytesObject buffer is null-terminated
        """
        # would raise ValueError: invalid literal for int() with base 10: b'1596728892'
        int(yjson.dumps(1596728892))

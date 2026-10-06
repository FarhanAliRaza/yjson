"""Feature compatibility, errors, and callback safety. Run with CPython 3.11 - 3.15."""
import dataclasses
import datetime as dt
import enum
import itertools
import json
import math
import os
from pathlib import Path
import sys
import unittest
import uuid
from typing import ClassVar

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import yjson
import orjson


@dataclasses.dataclass(slots=True)
class Record:
    id: int
    created: dt.datetime
    _private: str = "hidden"


class Color(enum.Enum):
    RED = "red"


class Code(enum.IntEnum):
    OK = 200


class Text(str):
    def __str__(self):
        return "override"


class Integer(int):
    def __int__(self):
        return 999


class Items(list):
    def __iter__(self):
        raise AssertionError("must use builtin layout")


class Mapping(dict):
    def items(self):
        raise AssertionError("must use builtin layout")


class Features(unittest.TestCase):
    def same(self, value, option=0, default=None):
        self.assertEqual(yjson.dumps(value, default=default, option=option),
                         orjson.dumps(value, default=default, option=option))

    def test_types_and_combinations(self):
        value = {"z": [Record(3, dt.datetime(2024, 1, 2, 3, 4, 5, 123456)),
                       dt.date(2024, 2, 29), dt.time(3, 4, 5, 1),
                       dt.datetime(2024, 1, 2, tzinfo=dt.timezone(dt.timedelta(hours=5, minutes=30))),
                       dt.datetime(2024, 1, 2, tzinfo=dt.timezone.utc),
                       uuid.UUID(int=12), Color.RED, Code.OK,
                       Text("é"), Integer(12), Items([1, 2]), Mapping(a=3)], "a": {}}
        flags = [yjson.OPT_INDENT_2, yjson.OPT_SORT_KEYS, yjson.OPT_NAIVE_UTC,
                 yjson.OPT_UTC_Z, yjson.OPT_OMIT_MICROSECONDS, yjson.OPT_APPEND_NEWLINE]
        for switches in itertools.product((False, True), repeat=len(flags)):
            option = sum(flag for flag, enabled in zip(flags, switches) if enabled)
            with self.subTest(option=option):
                self.same(value, option)

    def test_nonstr_keys_duplicates(self):
        value = {"1": "string", 1: "integer", None: 3, False: 4, 1.5: 5,
                 dt.date(2024, 1, 1): 6, uuid.UUID(int=0): 7, Color.RED: 8,
                 Text("subclass"): 9, float("inf"): 10, (1 << 64) - 1: 11}
        for option in (4, 5, 36, 37, 4 | 64):
            with self.subTest(option=option):
                self.same(value, option)
        self.assertEqual(yjson.dumps({Text("subclass"): 1}), b'{"subclass":1}')

    def test_nonstr_container_dispatch(self):
        # General-key tables remain general after deleting their non-string key.
        general = {0: "removed", "rows": [{"id": i, "text": "é\\\"\n"} for i in range(12)]}
        del general[0]
        split = vars(type("Fields", (), {})())
        split.update(a=1, rows=[{"b": 2}])
        marker = object()
        cases = [general, split, {"rows": [{"id": i} for i in range(12)]},
                 [{"a": {1: [{"z": 3}]}}], {Text("key"): [{"s": "subclass"}]},
                 {"a": marker}, [marker, {"after": "tail"}]]
        for option in (4, 5, 36, 37, 2564, 2565, 4 | 256, 4 | 64):
            for value in cases:
                with self.subTest(option=option, value=value):
                    self.same(value, option, lambda obj: {1: {"value": "converted"}})

    def test_sorted_native_entries(self):
        keys = ["a", "aa", "a\0", "é", "ÿ", "Ā", "߿", "ࠀ", "\uffff", "𐀀", "😀"]
        for count in (0, 1, 6, 16, 17, 32, 33, 65):
            value = {f"{keys[i % len(keys)]}{i}": {"z": [i, 0.1], "a": True}
                     for i in reversed(range(count))}
            for option in (32, 36, 2592, 2596, 32 | 64, 32 | 256):
                with self.subTest(count=count, option=option):
                    self.same(value, option)
        for key in ("\ud800", "x\udfff"):
            with self.assertRaises(TypeError):
                yjson.dumps({key: 1}, option=32)

    def test_sorted_entry_ownership(self):
        marker = object()
        for count in (6, 65):
            keys = [f"field_{i}" for i in range(count)]
            value = dict.fromkeys(keys, marker)
            references = sys.getrefcount(marker)
            for _ in range(20):
                with self.assertRaises(TypeError):
                    yjson.dumps(value, option=32)
                self.same(value, 32, lambda obj: "replacement")
            self.assertEqual(sys.getrefcount(marker), references)
            expected = orjson.dumps(dict.fromkeys(keys, "replacement"), option=32)
            def clear_source(obj):
                value.clear()
                return "replacement"
            self.assertEqual(yjson.dumps(value, option=32, default=clear_source), expected)
        value = {**dict.fromkeys((f"a{i}" for i in range(65)), marker), "\ud800": marker}
        references = sys.getrefcount(marker)
        with self.assertRaises(TypeError):
            yjson.dumps(value, option=32)
        self.assertEqual(sys.getrefcount(marker), references)

    def test_integer_limits(self):
        for number in (-(1 << 63), (1 << 63) - 1, 1 << 63, (1 << 64) - 1):
            self.same(number)
            self.same([number] * 12)
        for number in (-(1 << 63) - 1, 1 << 64):
            with self.assertRaises(yjson.JSONEncodeError):
                yjson.dumps(number)
        for number in (-(1 << 53) + 1, (1 << 53) - 1):
            self.same({"n": [number]}, yjson.OPT_STRICT_INTEGER)
        for number in (-(1 << 53), 1 << 53, (1 << 64) - 1):
            with self.assertRaises(TypeError):
                yjson.dumps({"n": [number]}, option=yjson.OPT_STRICT_INTEGER)

    def test_integer_digit_boundaries(self):
        import random
        rng = random.Random(1564)
        values = [sign * ((1 << bits) + offset)
                  for bits in (29, 30, 31, 59, 60, 61, 62, 63, 64, 89, 90, 100)
                  for offset in (-1, 0, 1) for sign in (-1, 1)]
        values.extend(rng.randrange(-(1 << 63), 1 << 64) for _ in range(1000))
        for value in values:
            if -(1 << 63) <= value < (1 << 64):
                for option in (0, 1, 4, 32):
                    self.same({"value": value}, option)
                self.same(value)
                self.same(Integer(value))
            else:
                for option in (0, 1, 4, 32, 2564):
                    with self.assertRaises(TypeError):
                        yjson.dumps({"value": value}, option=option)
                # Encoder state remains valid after an early range failure.
                self.assertEqual(yjson.dumps({"ok": True}), b'{"ok":true}')

    def test_datetime_boundaries(self):
        from zoneinfo import ZoneInfo
        for year in (1, 99, 999, 2024, 9999):
            self.same(dt.datetime(year, 1, 2, 3, 4, 5, 999999))
            self.same(dt.date(year, 1, 2))
        for seconds in (-61, -31, -30, -29, -1, 0, 1, 29, 30, 31, 61, 19800):
            value = dt.datetime(2024, 1, 1, tzinfo=dt.timezone(dt.timedelta(seconds=seconds)))
            for option in (0, 128, 8, 1 | 128):
                self.same(value, option)
                self.same({value: 1}, option | 4)
        for zone in ("UTC", "Asia/Karachi", "America/New_York"):
            self.same(dt.datetime(2024, 7, 2, tzinfo=ZoneInfo(zone)), 128)

    def test_dataclass_fields_and_nested_sort(self):
        @dataclasses.dataclass(frozen=True)
        class Payload:
            z: dict
            a: int
            init_only: dataclasses.InitVar[int] = 0
            class_only: ClassVar[int] = 10
            _private: str = "hidden"
        for option in (0, 1, 32, 33, 37):
            self.same(Payload({"z": 1, "a": 2}, 3), option)
        from collections import namedtuple
        Point = namedtuple("Point", "x y")
        with self.assertRaises(TypeError):
            yjson.dumps(Point(1, 2))
        self.same(Point(1, 2), default=lambda p: {"x": p.x, "y": p.y})

    def test_tuple_layouts(self):
        # Tuple items moved in CPython 3.14 (cached hash); cover every traversal that reads them.
        numeric = tuple(range(12)) + (0.5, -2.25, 1e300)
        value = {"empty": (), "one": ("x",), "mixed": (1, "two", None, True, 2.5, [3], {"k": (4,)}),
                 "numeric": numeric, "strings": tuple("abcdefghij"), "nested": ((), ((1,),), [(2, 3)])}
        for option in (0, 1, 32, 33, 4, 5, 36, 37, 1024, 64):
            with self.subTest(option=option):
                self.same(value, option)
        keyed = {1: (1, 2), "s": (3,), 2.5: ((4,), 5)}
        for option in (4, 5, 36, 37):
            with self.subTest(option=option):
                self.same(keyed, option)
        self.assertEqual(yjson.dumps_socket(value), yjson.dumps(value))
        self.assertEqual(yjson.dumps_socket(keyed), yjson.dumps(keyed, option=4))
        self.assertEqual(yjson.dumps_socket((1 << 70, "x")), b'[1180591620717411303424,"x"]')

    def test_uuid_range(self):
        # Full 128-bit range, including the sign bit position (PyLong_AsNativeBytes on 3.13+).
        values = [uuid.UUID(int=0), uuid.UUID(int=1), uuid.UUID(int=1 << 127), uuid.UUID(int=(1 << 128) - 1)]
        self.same(values)
        self.same({value: index for index, value in enumerate(values)}, yjson.OPT_NON_STR_KEYS)
        self.assertEqual(yjson.dumps(values[-1]), b'"ffffffff-ffff-ffff-ffff-ffffffffffff"')

    def test_default_and_passthrough(self):
        marker = object()
        self.same({"a": [marker]}, default=lambda o: {"value": 42})
        self.same(Record(1, dt.datetime(2024, 1, 1)), yjson.OPT_PASSTHROUGH_DATACLASS, lambda o: "record")
        self.same(dt.datetime.now(), yjson.OPT_PASSTHROUGH_DATETIME, lambda o: "datetime")
        self.same(Text("secret"), yjson.OPT_PASSTHROUGH_SUBCLASS, lambda o: "redacted")
        self.same(Items([1]), yjson.OPT_PASSTHROUGH_SUBCLASS, lambda o: "items")
        self.same(Code.OK, yjson.OPT_PASSTHROUGH_SUBCLASS)
        for option in (0, 1, 32, 256):
            with self.assertRaises(TypeError):
                yjson.dumps(marker, default=lambda o: o, option=option)
        error = ValueError("callback failed")
        def fail(obj):
            raise error
        with self.assertRaises(TypeError) as caught:
            yjson.dumps(marker, default=fail)
        self.assertIs(caught.exception.__cause__, error)
        self.assertEqual(yjson.dumps(1, default=42), b"1")

    def test_reentrant_and_temporary_keys(self):
        markers = [object() for _ in range(100)]
        def convert(obj):
            yjson.dumps({"a": {"b": "a different output"}})
            return {("temporary_%d" % markers.index(obj)): "result"}
        expected = json.dumps([convert(obj) for obj in markers], separators=(",", ":")).encode()
        self.assertEqual(yjson.dumps(markers, default=convert), expected)
        self.assertEqual(yjson.dumps({"a": markers, "after": {"a": "tail"}}, default=convert),
                         b'{"a":' + expected + b',"after":{"a":"tail"}}')
        def nested(obj):
            return json.loads(yjson.dumps(obj, default=lambda child: "inner"))
        self.assertEqual(yjson.dumps(markers[0], default=nested), b'"inner"')
        for option in (1, 5, 33, 37):
            self.same({"a": markers, "after": {"a": "tail"}}, option, convert)

    def test_indent_depth_and_key_cache(self):
        for depth in (0, 1, 2, 3, 4, 15, 63, 126):
            for size in (0, 1, 7, 31, 63, 127, 255, 4063):
                obj = {"key": "x" * size, "tail": []}
                for level in range(depth):
                    obj = [obj] if level % 2 else {"nested": obj}
                self.same(obj, 1)
        for size in (0, 1, 29, 30, 31, 32, 33, 255, 65532, 65536):
            key = "x" * size + "é\"\\\n😀"
            obj = [{key: value, "nested": {key: value}} for value in range(4)]
            for option in (1, 5, 33, 37, 2561, 65):
                self.same(obj, option)

    def test_mutating_callbacks(self):
        marker = object()
        for option in (0, 1, 32, 4, 5, 2564):
            value = [marker, 2]
            def clear_list(obj):
                value.clear()
                return 1
            with self.assertRaises(TypeError):
                yjson.dumps(value, default=clear_list, option=option)
            # Same-length mutation can reallocate a list's backing storage.
            value = [marker, 2]
            def replace_list(obj):
                value.extend(range(1000))
                del value[2:]
                return 1
            self.assertEqual(yjson.dumps(value, default=replace_list, option=option),
                             orjson.dumps([1, 2], option=option))
            # A callback can remove the last owner of the dict currently being encoded.
            root = [{"x": marker, "after": 2}]
            def remove_owner(obj):
                root.clear()
                return 1
            with self.assertRaises(TypeError):
                yjson.dumps(root, default=remove_owner, option=option)
            @dataclasses.dataclass
            class Getter:
                a: int = 1
                b: int = 2
                def __getattribute__(self, name):
                    if name == "a":
                        root.clear()
                    return object.__getattribute__(self, name)
            root = [Getter()]
            with self.assertRaises(TypeError):
                yjson.dumps(root, option=option)

    def test_callback_ancestor_storage(self):
        import gc
        import sys
        marker = object()
        nested = {"value": marker}
        root = [marker, nested]
        counts = [sys.getrefcount(value) for value in (root, nested, marker)]
        # The first callback uses one parent; the second uses the deeper path.
        for option in (0, 1, 4, 32, 2564):
            for _ in range(20):
                self.same(root, option, lambda obj: "converted")
            self.assertEqual([sys.getrefcount(value) for value in (root, nested, marker)], counts)
            root = [marker, {"value": marker}]
            calls = 0
            def clear_on_second(obj):
                nonlocal calls
                calls += 1
                if calls == 2:
                    root.clear()
                    gc.collect()
                return "converted"
            with self.assertRaises(TypeError):
                yjson.dumps(root, default=clear_on_second, option=option)
            root = [marker, nested]
            counts = [sys.getrefcount(value) for value in (root, nested, marker)]

    def test_option_fuzz(self):
        import random
        rng = random.Random(912)
        def value(depth=0):
            scalars = [None, True, False, 0, 2**63, -(2**63), 0.1, -0.0, "é\\\"\n😀"]
            if depth > 3 or rng.random() < 0.5:
                return rng.choice(scalars)
            if rng.random() < 0.5:
                return [value(depth + 1) for _ in range(rng.randrange(6))]
            return {str(rng.randrange(100)): value(depth + 1) for _ in range(rng.randrange(6))}
        for _ in range(500):
            obj = value()
            self.same(obj, rng.choice((1, 32, 33, 1024, 1057, 4, 5, 36, 37)))

    def test_fragment(self):
        for option in (0, 1, 32, 1024, 1 | 32 | 1024):
            self.assertEqual(yjson.dumps({"a": yjson.Fragment(b'{"x":  1}')}, option=option),
                             orjson.dumps({"a": orjson.Fragment(b'{"x":  1}')}, option=option))
        self.assertEqual(yjson.dumps(yjson.Fragment("true")), b"true")
        with self.assertRaises(TypeError):
            yjson.Fragment(42)
        damaged = yjson.Fragment(b"true")
        damaged._data = 42
        with self.assertRaises(TypeError):
            yjson.dumps(damaged)

    def test_errors_and_recovery(self):
        self.assertIs(yjson.JSONEncodeError, TypeError)
        for value in (object(), {1: 2}, "\ud800", {"\udfff": 1}, dt.time(tzinfo=dt.timezone.utc)):
            with self.assertRaises(TypeError):
                yjson.dumps(value)
            self.assertEqual(yjson.dumps({"ok": True}), b'{"ok":true}')
        recursive = []; recursive.append(recursive)
        for option in (0, 1, 32):
            with self.assertRaises(TypeError):
                yjson.dumps(recursive, option=option)
        for option in (-1, 4096, True, "1", 1 << 100):
            with self.assertRaises(TypeError):
                yjson.dumps(1, option=option)
        for call in (lambda: yjson.dumps(), lambda: yjson.dumps(1, None, 0, 2),
                     lambda: yjson.dumps(obj=1), lambda: yjson.dumps(1, nope=2),
                     lambda: yjson.dumps(1, None, default=None)):
            with self.assertRaises(TypeError):
                call()
        self.assertEqual(yjson.dumps(1, None, None), b"1")

    def test_numpy_options(self):
        import numpy as np
        for array in (np.array([[1, 2], [3, 4]]), np.array([0.1, 2.5]), np.array([], dtype=np.float64)):
            for option in (1, 32, 1 | 32, 64):
                self.same({"array": array}, option | yjson.OPT_SERIALIZE_NUMPY)
        self.assertEqual(yjson.dumps([math.nan, math.inf, -math.inf]), b"[NaN,Infinity,-Infinity]")

    def test_socket_big_integers(self):
        import random
        rng = random.Random(601)
        values = [sign * (2**bits + delta) for bits in (63, 64, 65, 89, 90, 100, 127, 128, 256, 1024)
                  for sign in (-1, 1) for delta in (-1, 0, 1)]
        values += [rng.getrandbits(rng.randrange(65, 4096)) * rng.choice((-1, 1)) for _ in range(200)]
        for value in values:
            for obj in (value, [1, value, None], {"n": Integer(value)}, {value: "key"}):
                self.assertEqual(json.loads(yjson.dumps_socket(obj)), json.loads(json.dumps(obj)))
        self.assertEqual(yjson.dumps_socket([None, math.nan, math.inf, -math.inf]),
                         b"[null,NaN,Infinity,-Infinity]")
        with self.assertRaises(TypeError):
            yjson.dumps(2**100)

    def test_socket_matches_stdlib_wire(self):
        # Strings pass through untouched and non-finite floats stay bare tokens, as json.dumps writes them.
        for text in ("__reflex_nan__", "__reflex_inf__", "__reflex_esc__x", "nan", "null", "NaN"):
            value = {text: [text, {"converted": object()}], "f": [math.nan, math.inf, -math.inf]}
            expected = {text: [text, {"converted": text}], "f": [math.nan, math.inf, -math.inf]}
            self.assertEqual(yjson.dumps_socket(value, default=lambda obj: text).decode(),
                             json.dumps(expected, separators=(",", ":")))

    def test_socket_surrogates_and_growth(self):
        values = ["\ud800", "\udfff", "é😀\udcff\\\"\n", "\ud800\udfff",
                  "x" * 4063 + "\ud800", "\ud800" * 65536]
        for text in values:
            for obj in (text, [0] * 10 + [text], {text: text}, {1: text}):
                out = yjson.dumps_socket(obj)
                out.decode("utf-8")
                self.assertEqual(json.loads(out), json.loads(json.dumps(obj)))
        self.assertEqual(json.loads(yjson.dumps_socket({"s": "__reflex_nan__\ud800"})),
                         {"s": "__reflex_nan__\ud800"})
        for text in values[:4]:
            with self.assertRaises(TypeError):
                yjson.dumps(text)

    def test_socket_callback_ownership_and_errors(self):
        import gc
        from unittest.mock import patch
        marker = object()
        root = [marker, {"value": marker}]
        with patch("json.dumps", side_effect=AssertionError("stdlib encoder used")):
            self.assertEqual(yjson.dumps_socket(root, default=lambda obj: {"big": 2**100}),
                             b'[{"big":1267650600228229401496703205376},{"value":{"big":1267650600228229401496703205376}}]')
        calls = 0
        def clear(obj):
            nonlocal calls
            calls += 1
            if calls == 2:
                root.clear()
                gc.collect()
            return "__reflex_nan__"
        with self.assertRaises(TypeError):
            yjson.dumps_socket(root, default=clear)
        for call in (lambda: yjson.dumps_socket(object()), lambda: yjson.dumps_socket(1, option=1),
                     lambda: yjson.dumps_socket(1, None, 0), lambda: yjson.dumps_socket()):
            with self.assertRaises(TypeError):
                call()
        self.assertEqual(yjson.dumps_socket({"ok": True}), b'{"ok":true}')

    def test_import_leaves_child_environment_unchanged(self):
        # The Mojo runtime setenv()s PYTHONPATH/PYTHONEXECUTABLE at startup; a venv
        # python started with them inherited loses its site-packages.
        import subprocess
        script = (
            "import json, os, subprocess, sys\n"
            f"sys.path.insert(0, {str(Path(yjson.__file__).parent)!r})\n"
            "before = dict(os.environ)\n"
            "import yjson\n"
            "child = subprocess.run([sys.executable, '-c', 'import json, os; print(json.dumps(dict(os.environ)))'],"
            " capture_output=True, text=True, check=True)\n"
            "print(json.dumps([before, json.loads(child.stdout), child.stderr]))\n"
        )
        env = {k: v for k, v in os.environ.items()
               if k not in ("MOJO_PYTHON_LIBRARY", "PYTHONEXECUTABLE", "PYTHONPATH")}
        result = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True, env=env, check=True)
        before, child, stderr = json.loads(result.stdout)
        self.assertEqual(stderr, "")
        self.assertEqual(child, before)

    @unittest.skipIf(sys.version_info < (3, 12), "CPython imports _pylong for enormous int(str) only from 3.12")
    def test_socket_enormous_integer_import_ownership(self):
        import builtins
        import gc
        from unittest.mock import patch
        limit = sys.get_int_max_str_digits()
        original_import = builtins.__import__
        previous_module = sys.modules.pop("_pylong", None)
        root = [1 << 200000, 2]
        called = False
        def intercept(name, *args, **kwargs):
            nonlocal called
            if name == "_pylong":
                called = True
                root.clear()
                gc.collect()
            return original_import(name, *args, **kwargs)
        try:
            sys.set_int_max_str_digits(0)
            with patch("builtins.__import__", side_effect=intercept):
                with self.assertRaises(TypeError):
                    yjson.dumps_socket(root)
            self.assertTrue(called)
            self.assertEqual(yjson.dumps_socket(2**100), str(2**100).encode())
        finally:
            sys.set_int_max_str_digits(limit)
            if previous_module is not None:
                sys.modules["_pylong"] = previous_module

    def test_loads(self):
        text = '{"é": [1, 0.1, null, true, "😀"], "big": 18446744073709551616}'
        for value in (text, text.encode(), bytearray(text.encode()), memoryview(text.encode())):
            self.assertEqual(yjson.loads(value), orjson.loads(value))
        for value in (b"\xff", '"\ud800"', '"\\ud800"', b"NaN", b"Infinity", b"1e400", b"{} trailing",
                      b"{", b"\xef\xbb\xbf{}", b"[1,]", memoryview(b"abcd")[::2], 42):
            with self.subTest(value=value), self.assertRaises(yjson.JSONDecodeError):
                yjson.loads(value)
        try:
            yjson.loads('{"a":}')
        except yjson.JSONDecodeError as error:
            self.assertEqual((error.pos, error.lineno, error.colno), (5, 1, 6))


if __name__ == "__main__":
    unittest.main()

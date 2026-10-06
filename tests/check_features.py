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
            # A dataclass with a __dict__ is read from that dict (as orjson reads it), so its
            # getters never run; a slots dataclass goes through getattr, and a getter that
            # removes the owner of the object being encoded is an error, not a crash.
            for slots in (False, True):
                @dataclasses.dataclass(slots=slots)
                class Getter:
                    a: int = 1
                    b: int = 2
                    def __getattribute__(self, name):
                        if name == "a":
                            root.clear()
                        return object.__getattribute__(self, name)
                root = [Getter()]
                if slots or option & yjson.OPT_PASSTHROUGH_DATACLASS:
                    with self.assertRaises(TypeError):
                        yjson.dumps(root, option=option)
                else:
                    self.assertEqual(yjson.dumps(root, option=option), orjson.dumps([{"a": 1, "b": 2}], option=option))

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

    def test_socket_classify_plans(self):
        @dataclasses.dataclass
        class Row:
            name: str
            _hidden: int
            when: dt.date

        class Wrapper:  # stands in for a proxy that forwards attribute access
            def __init__(self, inner):
                self._inner = inner
            def __getattr__(self, name):
                return getattr(self._inner, name)

        asked, defaulted = [], []
        def classify(kind):
            asked.append(kind)
            if kind in (Row, Wrapper):
                return ("name", "_hidden", "when")
            if kind is dt.date:
                return str
            if kind is set:
                return sorted
            return None
        def default(obj):
            defaulted.append(type(obj))
            return repr(obj)
        row = Row("a", 1, dt.date(2024, 1, 2))
        value = [row, Wrapper(Row("b", 2, dt.date(2024, 3, 4))), row, {3, 1}, complex(1, 2), complex(0, 1)]
        out = yjson.dumps_socket(value, default=default, classify=classify)
        self.assertEqual(out, b'[{"name":"a","_hidden":1,"when":"2024-01-02"},'
                              b'{"name":"b","_hidden":2,"when":"2024-03-04"},'
                              b'{"name":"a","_hidden":1,"when":"2024-01-02"},[1,3],"(1+2j)","1j"]')
        self.assertEqual(asked, [Row, dt.date, Wrapper, set, complex])  # once per type per call
        self.assertEqual(defaulted, [complex, complex])
        self.assertEqual(yjson.dumps_socket([row], default=str), yjson.dumps_socket([row], default=str, classify=None))
        # A plan's result is encoded like any value, including further classified objects.
        self.assertEqual(yjson.dumps_socket(row, classify=lambda kind: (lambda obj: [obj.when]) if kind is Row else str),
                         b'["2024-01-02"]')
        # The str plan writes naive dates and times natively; it must equal str().
        utc, plus = dt.timezone.utc, dt.timezone(dt.timedelta(hours=5, minutes=30))
        values = [dt.date(2024, 1, 2), dt.date(999, 12, 31), dt.datetime(2024, 1, 2, 3, 4, 5),
                  dt.datetime(2024, 1, 2, 3, 4, 5, 6), dt.datetime(1, 1, 1), dt.time(0, 0), dt.time(23, 59, 59, 999999),
                  dt.datetime(2024, 1, 2, 3, 4, tzinfo=utc), dt.datetime(2024, 1, 2, 3, 4, 5, 7, tzinfo=plus),
                  dt.time(1, 2, tzinfo=plus), dt.timedelta(days=1, seconds=1, microseconds=1)]
        self.assertEqual(json.loads(yjson.dumps_socket(values, classify=lambda kind: str)), [str(v) for v in values])
        # More types than cache slots still answer correctly.
        kinds = [type(f"K{i}", (), {"v": i}) for i in range(40)]
        out = yjson.dumps_socket([k() for k in kinds] * 2, classify=lambda kind: ("v",))
        self.assertEqual(json.loads(out), [{"v": i} for i in range(40)] * 2)
        for bad in (1, ("ok", 2), ["name"]):
            with self.assertRaises(TypeError):
                yjson.dumps_socket(row, classify=lambda kind, bad=bad: bad)
        def boom(kind):
            raise KeyError("boom")
        with self.assertRaises(TypeError) as caught:
            yjson.dumps_socket(row, classify=boom)
        self.assertIsInstance(caught.exception.__cause__, KeyError)
        with self.assertRaises(TypeError):
            yjson.dumps_socket(row, classify=("name",))
        with self.assertRaises(TypeError) as caught:
            yjson.dumps_socket(row, classify=lambda kind: ("missing",))
        self.assertIsInstance(caught.exception.__cause__, AttributeError)
        # dumps() has no classify; plain objects without a plan still need default.
        with self.assertRaises(TypeError):
            yjson.dumps(row, classify=classify)

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

    def test_numpy_dtypes_and_scalars(self):
        import numpy as np
        option = yjson.OPT_SERIALIZE_NUMPY
        values = [0, 1, 2, 3, 7, 8, 9, 10, 99, 100, 127]
        for dtype in ("i1", "i2", "i4", "i8", "u1", "u2", "u4", "u8", "f2", "f4", "f8", "?"):
            array = np.array(values, dtype=dtype)
            for shape in ((-1,), (1, -1), (11, 1)):
                for extra in (0, yjson.OPT_INDENT_2, yjson.OPT_SORT_KEYS, yjson.OPT_STRICT_INTEGER):
                    with self.subTest(dtype=dtype, shape=shape, option=extra):
                        self.same({"a": array.reshape(shape)}, option | extra)
            with self.subTest(dtype=dtype, scalar=True):
                self.same([array[3], array[-1]], option)
        self.same(np.array([-2**63, 2**63 - 1]), option)
        self.same(np.array([2**64 - 1], dtype="u8"), option)
        self.assertEqual(yjson.dumps(np.uint64(2**64 - 1)), b"18446744073709551615")
        self.same([np.empty((0, 4, 2)), np.empty((2, 0))], option | yjson.OPT_INDENT_2)

    def test_numpy_float32_shortest(self):
        # float32 and float16 use their own shortest round-trip digits, as orjson writes them.
        import numpy as np
        option = yjson.OPT_SERIALIZE_NUMPY
        rng = np.random.default_rng(32)
        bits = rng.integers(0, 2**32, size=200000, dtype=np.uint64).astype(np.uint32)
        f32 = bits.view(np.float32)
        f32 = f32[np.isfinite(f32)]
        self.same(f32, option)
        self.same(f32[:64].reshape(8, 8), option | yjson.OPT_INDENT_2)
        f16 = np.arange(65536, dtype=np.uint16).view(np.float16)
        self.same(f16[np.isfinite(f16)], option)
        for value in (3.4028235e38, 1e-45, 1e-7, 1e-6, 1e12, 1e13, 0.1, 1 / 3):
            with self.subTest(value=value):
                self.same(np.float32(value), option)
                if np.isfinite(np.float16(value)):
                    self.same(np.float16(value), option)
        self.assertEqual(yjson.dumps(np.array([1.0, 3.4028235e38], np.float32)), b"[1.0,3.4028235e+38]")

    def test_numpy_datetime64(self):
        import numpy as np
        option = yjson.OPT_SERIALIZE_NUMPY
        rng = np.random.default_rng(64)
        # The epoch neighbourhood and random instants, every unit, every datetime option.
        for unit, limit in (("Y", 8000), ("M", 96000), ("W", 400000), ("D", 2900000), ("h", 7 * 10**7),
                            ("m", 4 * 10**9), ("s", 2 * 10**11), ("ms", 2 * 10**14), ("us", 2 * 10**17), ("ns", 2**62)):
            low = 0 if unit in ("Y", "M") else -limit // 4  # year 0 is nearer than 9999; orjson aborts on negative months
            values = np.concatenate([np.arange(0, 40), rng.integers(low, limit, size=200)]).astype(np.int64)
            array = values.view(f"M8[{unit}]")
            for extra in (0, yjson.OPT_NAIVE_UTC, yjson.OPT_NAIVE_UTC | yjson.OPT_UTC_Z, yjson.OPT_OMIT_MICROSECONDS, yjson.OPT_INDENT_2):
                with self.subTest(unit=unit, option=extra):
                    self.same(array, option | extra)
                    self.same({"a": array.reshape(20, 12)}, option | extra)
                    self.same([array[0], array[-1]], option | extra)
        self.assertEqual(yjson.dumps(np.datetime64("1969-12", "M")), b'"1969-12-01T00:00:00"')
        self.assertEqual(yjson.dumps(np.datetime64("9999-12-31T23:59:59.999999", "us")), b'"9999-12-31T23:59:59.999999"')
        for bad in (np.datetime64("NaT", "s"), np.datetime64("-1"), np.datetime64("10000"), np.datetime64("2021-01-01T00:00:00.5", "ps"),
                    np.datetime64("2021-01-01T00:00:00", "10s"), np.array([np.datetime64("NaT", "D")])):
            with self.subTest(value=bad), self.assertRaises(TypeError):
                yjson.dumps(bad)

    def test_numpy_errors_follow_orjson(self):
        import numpy as np
        option = yjson.OPT_SERIALIZE_NUMPY
        fortran = np.array([[1, 2], [3, 4]], order="F")
        for array, message in ((fortran, "numpy array is not C contiguous; use ndarray.tolist() in default"),
                               (np.arange(6)[::2], "numpy array is not C contiguous; use ndarray.tolist() in default"),
                               (np.array(5), "unsupported datatype in numpy array"),
                               (np.array([1 + 2j]), "unsupported datatype in numpy array"),
                               (np.array(["a"]), "unsupported datatype in numpy array"),
                               (np.array([1, 2], dtype=">i4"), "numpy array is not native-endianness")):
            with self.subTest(array=array):
                with self.assertRaises(TypeError) as context:
                    yjson.dumps(array)
                self.assertEqual(str(context.exception), message)
        # default takes non-contiguous and unsupported arrays, but not byte-swapped ones
        for array in (fortran, np.arange(6)[::2], np.array(5), np.array([1 + 2j])):
            with self.subTest(array=array):
                self.same(array, option, default=lambda a: a.real if isinstance(a, complex) else a.tolist() if a.ndim else a.item())
        with self.assertRaises(TypeError):
            yjson.dumps(np.array([1, 2], dtype=">i4"), default=lambda a: 1)
        for scalar in (np.complex64(1), np.timedelta64(1, "s"), np.bytes_(b"a"), np.longdouble(1.5)):
            with self.subTest(scalar=scalar), self.assertRaises(TypeError) as context:
                yjson.dumps(scalar)
            self.assertEqual(str(context.exception), f"Type is not JSON serializable: {type(scalar).__module__}.{type(scalar).__name__}")
        self.same(np.str_("text"), option)

    def test_timezone_libraries(self):
        import zoneinfo

        class LikePytz(dt.tzinfo):
            """pytz-style zone: attached with tzinfo= it reports LMT, normalize() gives the real offset."""
            def utcoffset(self, value): return dt.timedelta(hours=8, minutes=6)
            def dst(self, value): return dt.timedelta(0)
            def tzname(self, value): return "LMT"
            def normalize(self, value): return value.replace(tzinfo=dt.timezone(dt.timedelta(hours=8)))

        when = dt.datetime(2018, 1, 1, 2, 3, 4, tzinfo=LikePytz())
        self.assertEqual(yjson.dumps(when), b'"2018-01-01T02:03:04+08:00"')
        self.same(when)
        self.same({when: 1}, yjson.OPT_NON_STR_KEYS)
        for zone in ("Europe/Amsterdam", "Asia/Kolkata", "America/New_York", "Australia/Adelaide", "UTC"):
            for value in (dt.datetime(2024, 7, 2, 3, 4, 5, 6, tzinfo=zoneinfo.ZoneInfo(zone)), dt.datetime(1937, 1, 1, 12, tzinfo=zoneinfo.ZoneInfo(zone))):
                for option in (0, yjson.OPT_UTC_Z, yjson.OPT_OMIT_MICROSECONDS | yjson.OPT_UTC_Z):
                    with self.subTest(zone=zone, option=option):
                        self.same([value], option)
        self.same(dt.datetime(2024, 1, 2, tzinfo=dt.timezone(dt.timedelta(hours=-5, minutes=-30))))

    def test_orjson_api_details(self):
        import inspect
        self.assertEqual(str(inspect.signature(yjson.dumps)), "(obj, /, default=None, option=None)")
        self.assertEqual((yjson.dumps.__module__, yjson.loads.__module__, yjson.Fragment.__module__), ("yjson", "yjson", "yjson"))
        self.assertRegex(yjson.__version__, r"^\d+\.\d+(\.\d+)?$")
        with self.assertRaises(TypeError) as context:
            yjson.dumps(yjson.Fragment("\ud800"))
        self.assertEqual(str(context.exception), "str is not valid UTF-8: surrogates not allowed")

        class Custom:
            pass

        class Recursive:
            def __init__(self, depth): self.depth = depth

        def unwrap(value): return Recursive(value.depth - 1) if value.depth else 0
        self.assertEqual(yjson.dumps(Recursive(254), default=unwrap), b"0")
        self.assertEqual(yjson.dumps([Recursive(254), Recursive(254)], default=unwrap), b"[0,0]")
        with self.assertRaises(TypeError) as context:
            yjson.dumps(Recursive(255), default=unwrap)
        self.assertEqual(str(context.exception), "default serializer exceeds recursion limit")

        def failing(value): raise KeyError("missing")
        with self.assertRaises(TypeError) as context:
            yjson.dumps(Custom(), default=failing)
        self.assertEqual(str(context.exception), "Type is not JSON serializable: Custom")
        self.assertIsInstance(context.exception.__cause__, KeyError)

        class Subclass(uuid.UUID):
            pass

        with self.assertRaises(TypeError):
            yjson.dumps(Subclass(int=5))
        self.same(Subclass(int=5), default=str)
        with self.assertRaises(TypeError):
            yjson.dumps({Subclass(int=5): 1}, option=yjson.OPT_NON_STR_KEYS)
        self.assertIsNotNone(yjson.loads(b"[" * 1024 + b"]" * 1024))
        with self.assertRaises(yjson.JSONDecodeError):
            yjson.loads(b"[" * 1025 + b"]" * 1025)


if __name__ == "__main__":
    unittest.main()

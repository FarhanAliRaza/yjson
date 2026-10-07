"""Checks loads(type=...): dataclasses, containers, Optional, defaults, errors and memory.

    python tests/check_typed_loads.py
"""
import dataclasses
import gc
import inspect
import sys
import unittest
from pathlib import Path
from typing import Any, Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import yjson


@dataclasses.dataclass
class Address:
    city: str
    zip: str = "00000"


@dataclasses.dataclass(slots=True)
class Record:
    id: int
    name: str
    active: bool
    score: float
    tags: list[str]
    address: Address
    note: Optional[str] = None
    extra: dict[str, Any] = dataclasses.field(default_factory=dict)
    computed: str = dataclasses.field(init=False, default="")

    def __post_init__(self):
        self.computed = self.name.upper()


@dataclasses.dataclass(frozen=True)
class Frozen:
    value: int
    label: str = "x"


@dataclasses.dataclass
class Node:
    value: int
    children: list["Node"] = dataclasses.field(default_factory=list)
    parent_ref: Optional["Node"] = None


@dataclasses.dataclass
class Unset:
    a: int
    b: int = dataclasses.field(init=False)


class Plain:
    pass


RECORD = b'{"id": 1, "name": "ada", "active": true, "score": 2, "tags": ["a", "\\u00e9"], "address": {"city": "Lahore"}, "unknown": [1, {"x": null}]}'


class TestTypedLoads(unittest.TestCase):
    def test_signature(self):
        self.assertEqual(str(inspect.signature(yjson.loads)), "(obj, /, *, type=None)")
        self.assertEqual(yjson.loads(RECORD, type=None), yjson.loads(RECORD))
        self.assertEqual(yjson.loads(RECORD, type=Any), yjson.loads(RECORD))
        with self.assertRaises(TypeError):
            yjson.loads(RECORD, kind=Record)
        with self.assertRaises(TypeError):
            yjson.loads(RECORD, Record)
        with self.assertRaises(TypeError):
            yjson.loads()

    def test_dataclass(self):
        record = yjson.loads(RECORD, type=Record)
        self.assertIsInstance(record, Record)
        self.assertEqual((record.id, record.name, record.active, record.score, record.tags), (1, "ada", True, 2.0, ["a", "é"]))
        self.assertIs(type(record.score), float)
        self.assertEqual(record.address, Address("Lahore", "00000"))
        self.assertIsNone(record.note)
        self.assertEqual(record.extra, {})
        self.assertEqual(record.computed, "ADA")  # __post_init__ ran
        self.assertIsNot(yjson.loads(RECORD, type=Record).extra, record.extra)  # the factory runs per instance
        for variant in (RECORD.decode(), bytearray(RECORD), memoryview(RECORD)):
            self.assertEqual(yjson.loads(variant, type=Record), record)

    def test_plain_frozen_and_unset(self):
        self.assertEqual(yjson.loads('{"city": "a", "zip": "b", "zip": "c"}', type=Address), Address("a", "c"))
        frozen = yjson.loads('{"value": 3}', type=Frozen)
        self.assertEqual(frozen, Frozen(3))
        with self.assertRaises(dataclasses.FrozenInstanceError):
            frozen.value = 4
        unset = yjson.loads('{"a": 1}', type=Unset)
        self.assertEqual(unset.a, 1)
        self.assertFalse(hasattr(unset, "b"))
        self.assertEqual(yjson.loads('{"a": 1, "b": 2}', type=Unset).b, 2)

    def test_containers(self):
        records = yjson.loads(b"[" + RECORD + b"," + RECORD + b"]", type=list[Record])
        self.assertEqual(len(records), 2)
        self.assertEqual(records[1].address.city, "Lahore")
        self.assertEqual(yjson.loads('{"k": {"value": 1}}', type=dict[str, Node]), {"k": Node(1)})
        self.assertEqual(yjson.loads("[1, 2.5]", type=list[float]), [1.0, 2.5])
        self.assertEqual(yjson.loads("[[1], []]", type=list[list[int]]), [[1], []])
        self.assertEqual(yjson.loads('{"a": [1]}', type=dict), {"a": [1]})
        self.assertEqual(yjson.loads("[null, 1]", type=list[Optional[int]]), [None, 1])
        self.assertEqual(yjson.loads("[null, 1]", type=list[int | None]), [None, 1])
        self.assertIsNone(yjson.loads("null", type=Optional[Record]))
        self.assertEqual(yjson.loads('"é"', type=str), "é")
        self.assertIs(yjson.loads("true", type=bool), True)
        self.assertIsNone(yjson.loads("null", type=type(None)))

    def test_recursive(self):
        tree = yjson.loads('{"value": 1, "children": [{"value": 2, "children": [{"value": 3}]}]}', type=Node)
        self.assertEqual(tree, Node(1, [Node(2, [Node(3)])]))
        deep = "[" * 1024 + "]" * 1024
        self.assertEqual(len(yjson.loads(deep, type=list[Any])), 1)
        with self.assertRaises(yjson.JSONDecodeError):
            yjson.loads("[" * 1025 + "]" * 1025, type=list[Any])
        # Each level is an object and an array: 511 levels plus the leaf nest 1,023 deep.
        text = '{"value": 0, "children": [' * 511 + '{"value": 1}' + "]}" * 511
        self.assertEqual(yjson.loads(text, type=Node).children[0].value, 0)
        with self.assertRaises(yjson.JSONDecodeError):
            yjson.loads('{"value": 0, "children": [' * 512 + '{"value": 1}' + "]}" * 512, type=Node)

    def test_errors(self):
        cases = [
            ('{"id": "x"}', Record, "expected int, got str", 7),
            ('{"id": 1.5}', Record, "expected int, got float", 7),
            ('{"name": "a"}', Record, "missing required field 'id' of Record", 12),
            ("[1]", Record, "expected Record, got array", 0),
            ('"s"', int, "expected int, got str", 0),
            ("1", str, "expected str, got number", 0),
            ("[true]", list[int], "expected int, got bool", 1),
            ("[1, 2]", list[str], "expected str, got number", 1),
            ("1", bool, "expected bool, got number", 0),
            ("null", int, "expected int, got null", 0),
            ("1", Optional[str], "expected str, got number", 0),
            ('{"a": 1}', dict[str, str], "expected str, got number", 6),
            ('{"value": 1, "children": [{}]}', Node, "missing required field 'value' of Node", 27),
            ('{"id": 1', Record, "unexpected end of data", 8),
            ('{"id": 1,}', Record, "trailing comma is not allowed", 8),
            ('{"id": 1 "name"}', Record, "unexpected character, expected ',' or '}'", 9),
            ('{"id": 1, "unknown": [1,]}', Record, "trailing comma is not allowed", 23),
            ('{"id": 1, "name": "\\ud800"}', Record, "no low surrogate in string", 19),
            ("", Record, "Input is a zero-length, empty document", 0),
            ("18446744073709551616", int, "expected int, got float", 0),
        ]
        for text, tp, message, pos in cases:
            with self.subTest(text=text), self.assertRaises(yjson.JSONDecodeError) as context:
                yjson.loads(text, type=tp)
            self.assertEqual((context.exception.msg, context.exception.pos), (message, pos))
        self.assertIsInstance(context.exception, ValueError)
        with self.assertRaises(yjson.JSONDecodeError):
            yjson.loads(42, type=Record)

    def test_unsupported_annotations(self):
        for tp in (set[int], tuple, tuple[int, ...], "Record", Plain, int | str, dict[int, str], dataclasses.dataclass(type("WithInitVar", (), {"__annotations__": {"a": dataclasses.InitVar[int]}}))):
            with self.subTest(tp=tp), self.assertRaises(TypeError):
                yjson.loads("1", type=tp)

    def test_defaults_are_not_shared_and_raise_propagates(self):
        @dataclasses.dataclass
        class Boom:
            value: int = dataclasses.field(default_factory=lambda: 1 / 0)
        with self.assertRaises(ZeroDivisionError):
            yjson.loads("{}", type=Boom)

        @dataclasses.dataclass
        class Post:
            value: int

            def __post_init__(self):
                raise ValueError("post")
        with self.assertRaises(ValueError):
            yjson.loads('{"value": 1}', type=Post)

    def test_failed_type_build_leaves_other_plans_intact(self):
        @dataclasses.dataclass
        class Good:
            a: int

        @dataclasses.dataclass
        class Bad:
            g: Good
            s: set[int]

        def factory():
            with self.assertRaises(TypeError):
                yjson.loads("{}", type=Bad)  # fails while Outer's plan is in use
            return 5

        @dataclasses.dataclass
        class Outer:
            items: list[Good]
            n: int = dataclasses.field(default_factory=factory)

        self.assertEqual(yjson.loads('{"a": 1}', type=Good), Good(1))
        for _ in range(3):
            with self.assertRaises(TypeError):
                yjson.loads('{"g": {"a": 1}, "s": []}', type=Bad)
        self.assertEqual(yjson.loads('[{"a": 3}]', type=list[Good]), [Good(3)])
        outer = yjson.loads('{"items": [{"a": 1}, {"a": 2}]}', type=Outer)
        self.assertEqual((outer.items, outer.n), ([Good(1), Good(2)], 5))
        gc.collect()

    def test_keys_with_escapes_and_non_ascii(self):
        @dataclasses.dataclass
        class Odd:
            café: int
            plain: int = 0
        self.assertEqual(yjson.loads('{"caf\\u00e9": 1, "pl\\u0061in": 2}', type=Odd), Odd(1, 2))
        self.assertEqual(yjson.loads('{"café": 1}'.encode(), type=Odd), Odd(1))
        with self.assertRaises(yjson.JSONDecodeError):
            yjson.loads('{"plain": 2}', type=Odd)

    def test_memory(self):
        try:
            import psutil
        except ImportError:
            self.skipTest("psutil")
        doc = b"[" + b",".join([RECORD] * 200) + b"]"
        for tp in (list[Record], list[Node], list[Any]):
            with self.subTest(tp=tp):
                try:
                    yjson.loads(doc, type=tp)
                except yjson.JSONDecodeError:
                    pass
                gc.collect()
                before = psutil.Process().memory_info().rss
                for _ in range(2000):
                    try:
                        yjson.loads(doc, type=tp)
                    except yjson.JSONDecodeError:
                        pass
                gc.collect()
                self.assertLess(psutil.Process().memory_info().rss - before, 4 * 1024 * 1024)
        gc.collect()
        before = psutil.Process().memory_info().rss
        for _ in range(20000):
            try:
                yjson.loads('{"id": "x"}', type=Record)
            except yjson.JSONDecodeError:
                pass
        self.assertLess(psutil.Process().memory_info().rss - before, 4 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MPL-2.0
# Copyright ijl (2019-2026)

import abc
import uuid
from dataclasses import InitVar, asdict, dataclass, field
from enum import Enum
from typing import ClassVar, Optional

import pytest

import yjson


class AnEnum(Enum):
    ONE = 1
    TWO = 2


@dataclass
class EmptyDataclass:
    pass


@dataclass
class EmptyDataclassSlots:
    __slots__ = ()


@dataclass
class Dataclass1:
    name: str
    number: int
    sub: Optional["Dataclass1"]


@dataclass
class Dataclass2:
    name: str | None = field(default="?")


@dataclass
class Dataclass3:
    a: str
    b: int
    c: dict
    d: bool
    e: float
    f: list
    g: tuple


@dataclass
class Dataclass4:
    a: str = field()
    b: int = field(metadata={"unrelated": False})
    c: float = 1.1


@dataclass
class Datasubclass(Dataclass1):
    additional: bool


@dataclass
class Slotsdataclass:
    __slots__ = ("_c", "a", "b", "d")
    a: str
    b: int
    _c: str
    d: InitVar[str]
    cls_var: ClassVar[str] = "cls"


@dataclass
class Defaultdataclass:
    a: uuid.UUID
    b: AnEnum


@dataclass
class UnsortedDataclass:
    c: int
    b: int
    a: int
    d: dict | None


@dataclass
class InitDataclass:
    a: InitVar[str]
    b: InitVar[str]
    cls_var: ClassVar[str] = "cls"
    ab: str = ""

    def __post_init__(self, a: str, b: str):
        self._other = 1
        self.ab = f"{a} {b}"


class AbstractBase(abc.ABC):
    @abc.abstractmethod
    def key(self):
        raise NotImplementedError


@dataclass(frozen=True)
class ConcreteAbc(AbstractBase):
    __slots__ = ("attr",)

    attr: float

    def key(self):
        return "dkjf"


class TestDataclass:
    def test_dataclass(self):
        """
        dumps() dataclass
        """
        obj = Dataclass1("a", 1, None)
        assert yjson.dumps(obj) == b'{"name":"a","number":1,"sub":null}'

    def test_dataclass_recursive(self):
        """
        dumps() dataclass recursive
        """
        obj = Dataclass1("a", 1, Dataclass1("b", 2, None))
        assert (
            yjson.dumps(obj)
            == b'{"name":"a","number":1,"sub":{"name":"b","number":2,"sub":null}}'
        )

    def test_dataclass_circular(self):
        """
        dumps() dataclass circular
        """
        obj1 = Dataclass1("a", 1, None)
        obj2 = Dataclass1("b", 2, obj1)
        obj1.sub = obj2
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(obj1)

    def test_dataclass_empty(self):
        """
        dumps() no attributes
        """
        assert yjson.dumps(EmptyDataclass()) == b"{}"

    def test_dataclass_empty_slots(self):
        """
        dumps() no attributes slots
        """
        assert yjson.dumps(EmptyDataclassSlots()) == b"{}"

    def test_dataclass_default_arg(self):
        """
        dumps() dataclass default arg
        """
        obj = Dataclass2()
        assert yjson.dumps(obj) == b'{"name":"?"}'

    def test_dataclass_types(self):
        """
        dumps() dataclass types
        """
        obj = Dataclass3("a", 1, {"a": "b"}, True, 1.1, [1, 2], (3, 4))
        assert (
            yjson.dumps(obj)
            == b'{"a":"a","b":1,"c":{"a":"b"},"d":true,"e":1.1,"f":[1,2],"g":[3,4]}'
        )

    def test_dataclass_metadata(self):
        """
        dumps() dataclass metadata
        """
        obj = Dataclass4("a", 1, 2.1)
        assert yjson.dumps(obj) == b'{"a":"a","b":1,"c":2.1}'

    def test_dataclass_classvar(self):
        """
        dumps() dataclass class variable
        """
        obj = Dataclass4("a", 1)
        assert yjson.dumps(obj) == b'{"a":"a","b":1,"c":1.1}'

    def test_dataclass_subclass(self):
        """
        dumps() dataclass subclass
        """
        obj = Datasubclass("a", 1, None, False)
        assert (
            yjson.dumps(obj)
            == b'{"name":"a","number":1,"sub":null,"additional":false}'
        )

    def test_dataclass_slots(self):
        """
        dumps() dataclass with __slots__ does not include under attributes, InitVar, or ClassVar
        """
        obj = Slotsdataclass("a", 1, "c", "d")
        assert "__dict__" not in dir(obj)
        assert yjson.dumps(obj) == b'{"a":"a","b":1}'

    def test_dataclass_default(self):
        """
        dumps() dataclass with default
        """

        def default(__obj):
            if isinstance(__obj, uuid.UUID):
                return str(__obj)
            elif isinstance(__obj, Enum):
                return __obj.value

        obj = Defaultdataclass(
            uuid.UUID("808989c0-00d5-48a8-b5c4-c804bf9032f2"),
            AnEnum.ONE,
        )
        assert (
            yjson.dumps(obj, default=default)
            == b'{"a":"808989c0-00d5-48a8-b5c4-c804bf9032f2","b":1}'
        )

    def test_dataclass_sort(self):
        """
        OPT_SORT_KEYS has no effect on dataclasses
        """
        obj = UnsortedDataclass(1, 2, 3, None)
        assert (
            yjson.dumps(obj, option=yjson.OPT_SORT_KEYS)
            == b'{"c":1,"b":2,"a":3,"d":null}'
        )

    def test_dataclass_sort_sub(self):
        """
        dataclass fast path does not prevent OPT_SORT_KEYS from cascading
        """
        obj = UnsortedDataclass(1, 2, 3, {"f": 2, "e": 1})
        assert (
            yjson.dumps(obj, option=yjson.OPT_SORT_KEYS)
            == b'{"c":1,"b":2,"a":3,"d":{"e":1,"f":2}}'
        )

    def test_dataclass_under(self):
        """
        dumps() does not include under attributes, InitVar, or ClassVar
        """
        obj = InitDataclass("zxc", "vbn")
        assert yjson.dumps(obj) == b'{"ab":"zxc vbn"}'

    def test_dataclass_option(self):
        """
        dumps() accepts deprecated OPT_SERIALIZE_DATACLASS
        """
        obj = Dataclass1("a", 1, None)
        assert (
            yjson.dumps(obj, option=yjson.OPT_SERIALIZE_DATACLASS)
            == b'{"name":"a","number":1,"sub":null}'
        )


class TestDataclassPassthrough:
    def test_dataclass_passthrough_raise(self):
        """
        dumps() dataclass passes to default with OPT_PASSTHROUGH_DATACLASS
        """
        obj = Dataclass1("a", 1, None)
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(obj, option=yjson.OPT_PASSTHROUGH_DATACLASS)
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(
                InitDataclass("zxc", "vbn"),
                option=yjson.OPT_PASSTHROUGH_DATACLASS,
            )

    def test_dataclass_passthrough_default(self):
        """
        dumps() dataclass passes to default with OPT_PASSTHROUGH_DATACLASS
        """
        obj = Dataclass1("a", 1, None)
        assert (
            yjson.dumps(obj, option=yjson.OPT_PASSTHROUGH_DATACLASS, default=asdict)
            == b'{"name":"a","number":1,"sub":null}'
        )

        def default(obj):
            if isinstance(obj, Dataclass1):
                return {"name": obj.name, "number": obj.number}
            raise TypeError

        assert (
            yjson.dumps(obj, option=yjson.OPT_PASSTHROUGH_DATACLASS, default=default)
            == b'{"name":"a","number":1}'
        )


class TestAbstractDataclass:
    def test_dataclass_abc(self):
        obj = ConcreteAbc(1.0)
        assert yjson.dumps(obj) == b'{"attr":1.0}'

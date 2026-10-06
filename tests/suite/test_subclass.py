# SPDX-License-Identifier: MPL-2.0
# Copyright ijl (2018-2022)

import collections
import json

import pytest

import yjson


class SubStr(str):
    pass


class SubInt(int):
    pass


class SubDict(dict):
    pass


class SubList(list):
    pass


class SubFloat(float):
    pass


class SubTuple(tuple):
    pass


class TestSubclass:
    def test_subclass_str(self):
        assert yjson.dumps(SubStr("zxc")) == b'"zxc"'

    def test_subclass_str_invalid(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubStr("\ud800"))

    def test_subclass_int(self):
        assert yjson.dumps(SubInt(1)) == b"1"

    def test_subclass_int_64(self):
        for val in (9223372036854775807, -9223372036854775807):
            assert yjson.dumps(SubInt(val)) == str(val).encode("utf-8")

    def test_subclass_int_53(self):
        for val in (9007199254740992, -9007199254740992):
            with pytest.raises(yjson.JSONEncodeError):
                yjson.dumps(SubInt(val), option=yjson.OPT_STRICT_INTEGER)

    def test_subclass_dict(self):
        assert yjson.dumps(SubDict({"a": "b"})) == b'{"a":"b"}'

    def test_subclass_list(self):
        assert yjson.dumps(SubList(["a", "b"])) == b'["a","b"]'
        ref = [True] * 512
        assert yjson.loads(yjson.dumps(SubList(ref))) == ref

    def test_subclass_float(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubFloat(1.1))
        assert json.dumps(SubFloat(1.1)) == "1.1"

    def test_subclass_tuple(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubTuple((1, 2)))
        assert json.dumps(SubTuple((1, 2))) == "[1, 2]"

    def test_namedtuple(self):
        Point = collections.namedtuple("Point", ["x", "y"])
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(Point(1, 2))

    def test_subclass_circular_dict(self):
        obj = SubDict({})
        obj["obj"] = obj
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(obj)

    def test_subclass_circular_list(self):
        obj = SubList([])
        obj.append(obj)
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(obj)

    def test_subclass_circular_nested(self):
        obj = SubDict({})
        obj["list"] = SubList([{"obj": obj}])
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(obj)


class TestSubclassPassthrough:
    def test_subclass_str(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubStr("zxc"), option=yjson.OPT_PASSTHROUGH_SUBCLASS)

    def test_subclass_int(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubInt(1), option=yjson.OPT_PASSTHROUGH_SUBCLASS)

    def test_subclass_dict(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubDict({"a": "b"}), option=yjson.OPT_PASSTHROUGH_SUBCLASS)

    def test_subclass_list(self):
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(SubList(["a", "b"]), option=yjson.OPT_PASSTHROUGH_SUBCLASS)

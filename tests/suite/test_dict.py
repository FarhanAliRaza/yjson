# SPDX-License-Identifier: (Apache-2.0 OR MIT)
# Copyright ijl (2018-2025), J. Nick Koston (2022), Anders Kaseorg (2022)

import pytest

import yjson


class TestDict:
    def test_dict(self):
        """
        dict
        """
        obj = {"key": "value"}
        ref = '{"key":"value"}'
        assert yjson.dumps(obj) == ref.encode("utf-8")
        assert yjson.loads(ref) == obj

    def test_dict_duplicate_loads(self):
        assert yjson.loads(b'{"1":true,"1":false}') == {"1": False}

    def test_dict_empty(self):
        obj = [{"key": [{}] * 4096}] * 4096  # type:ignore
        assert yjson.loads(yjson.dumps(obj)) == obj

    def test_dict_large_dict(self):
        """
        dict with >512 keys
        """
        obj = {f"key_{idx}": [{}, {"a": [{}, {}, {}]}, {}] for idx in range(513)}  # type: ignore
        assert len(obj) == 513
        assert yjson.loads(yjson.dumps(obj)) == obj

    def test_dict_large_4096(self):
        """
        dict with >4096 keys
        """
        obj = {f"key_{idx}": f"value_{idx}" for idx in range(4097)}
        assert len(obj) == 4097
        assert yjson.loads(yjson.dumps(obj)) == obj

    def test_dict_large_65536(self):
        """
        dict with >65536 keys
        """
        obj = {f"key_{idx}": f"value_{idx}" for idx in range(65537)}
        assert len(obj) == 65537
        assert yjson.loads(yjson.dumps(obj)) == obj

    def test_dict_large_keys(self):
        """
        dict with keys too large to cache
        """
        obj = {
            "keeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeey": "value",
        }
        ref = '{"keeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeey":"value"}'
        assert yjson.dumps(obj) == ref.encode("utf-8")
        assert yjson.loads(ref) == obj

    def test_dict_unicode(self):
        """
        dict unicode keys
        """
        obj = {"🐈": "value"}
        ref = b'{"\xf0\x9f\x90\x88":"value"}'
        assert yjson.dumps(obj) == ref
        assert yjson.loads(ref) == obj
        assert yjson.loads(ref)["🐈"] == "value"

    def test_dict_invalid_key_dumps(self):
        """
        dict invalid key dumps()
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps({1: "value"})
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps({b"key": "value"})

    def test_dict_invalid_key_loads(self):
        """
        dict invalid key loads()
        """
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads('{1:"value"}')
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads('{{"a":true}:true}')

    def test_dict_similar_keys(self):
        """
        loads() similar keys

        This was a regression in 3.4.2 caused by using
        the implementation in wy instead of wyhash.
        """
        assert yjson.loads(
            '{"cf_status_firefox67": "---", "cf_status_firefox57": "verified"}',
        ) == {"cf_status_firefox57": "verified", "cf_status_firefox67": "---"}

    def test_dict_pop_replace_first(self):
        "Test pop and replace a first key in a dict with other keys."
        data = {"id": "any", "other": "any"}
        data.pop("id")
        assert yjson.dumps(data) == b'{"other":"any"}'
        data["id"] = "new"
        assert yjson.dumps(data) == b'{"other":"any","id":"new"}'

    def test_dict_pop_replace_last(self):
        "Test pop and replace a last key in a dict with other keys."
        data = {"other": "any", "id": "any"}
        data.pop("id")
        assert yjson.dumps(data) == b'{"other":"any"}'
        data["id"] = "new"
        assert yjson.dumps(data) == b'{"other":"any","id":"new"}'

    def test_dict_pop(self):
        "Test pop and replace a key in a dict with no other keys."
        data = {"id": "any"}
        data.pop("id")
        assert yjson.dumps(data) == b"{}"
        data["id"] = "new"
        assert yjson.dumps(data) == b'{"id":"new"}'

    def test_in_place(self):
        "Mutate dict in-place"
        data = {"id": "any", "static": "msg"}
        data["id"] = "new"
        assert yjson.dumps(data) == b'{"id":"new","static":"msg"}'

    def test_dict_0xff(self):
        "dk_size <= 0xff"
        data = {str(idx): idx for idx in range(0xFF)}
        data.pop("112")
        data["112"] = 1
        data["113"] = 2
        assert yjson.loads(yjson.dumps(data)) == data

    def test_dict_0xff_repeated(self):
        "dk_size <= 0xff repeated"
        for _ in range(100):
            data = {str(idx): idx for idx in range(0xFF)}
            data.pop("112")
            data["112"] = 1
            data["113"] = 2
            assert yjson.loads(yjson.dumps(data)) == data

    def test_dict_0xffff(self):
        "dk_size <= 0xffff"
        data = {str(idx): idx for idx in range(0xFFFF)}
        data.pop("112")
        data["112"] = 1
        data["113"] = 2
        assert yjson.loads(yjson.dumps(data)) == data

    def test_dict_0xffff_repeated(self):
        "dk_size <= 0xffff repeated"
        for _ in range(100):
            data = {str(idx): idx for idx in range(0xFFFF)}
            data.pop("112")
            data["112"] = 1
            data["113"] = 2
            assert yjson.loads(yjson.dumps(data)) == data

    def test_dict_dict(self):
        class C:
            def __init__(self):
                self.a = 0
                self.b = 1

        assert yjson.dumps(C().__dict__) == b'{"a":0,"b":1}'

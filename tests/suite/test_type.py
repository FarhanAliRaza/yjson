# SPDX-License-Identifier: MPL-2.0
# Copyright ijl (2018-2026)

import io
import sys

import pytest

import yjson

from .util import SUPPORTS_BYTEARRAY, SUPPORTS_MEMORYVIEW


class TestType:
    def test_fragment(self):
        """
        yjson.JSONDecodeError on fragments
        """
        for val in ("n", "{", "[", "t"):
            pytest.raises(yjson.JSONDecodeError, yjson.loads, val)

    def test_invalid(self):
        """
        yjson.JSONDecodeError on invalid
        """
        for val in ('{"age", 44}', "[31337,]", "[,31337]", "[]]", "[,]"):
            pytest.raises(yjson.JSONDecodeError, yjson.loads, val)

    def test_str(self):
        """
        str
        """
        for obj, ref in (("blah", b'"blah"'), ("東京", b'"\xe6\x9d\xb1\xe4\xba\xac"')):
            assert yjson.dumps(obj) == ref
            assert yjson.loads(ref) == obj

    def test_str_latin1(self):
        """
        str latin1
        """
        assert yjson.loads(yjson.dumps("üýþÿ")) == "üýþÿ"

    def test_str_long(self):
        """
        str long
        """
        for obj in ("aaaa" * 1024, "üýþÿ" * 1024, "好" * 1024, "�" * 1024):
            assert yjson.loads(yjson.dumps(obj)) == obj

    def test_str_2mib(self):
        ref = '🐈🐈🐈🐈🐈"üýa0s9999🐈🐈🐈🐈🐈9\0999\\9999' * 1024 * 50
        assert yjson.loads(yjson.dumps(ref)) == ref

    def test_str_very_long(self):
        """
        str long enough to trigger overflow in bytecount
        """
        for obj in ("aaaa" * 20000, "üýþÿ" * 20000, "好" * 20000, "�" * 20000):
            assert yjson.loads(yjson.dumps(obj)) == obj

    def test_str_replacement(self):
        """
        str roundtrip �
        """
        assert yjson.dumps("�") == b'"\xef\xbf\xbd"'
        assert yjson.loads(b'"\xef\xbf\xbd"') == "�"

    def test_str_trailing_4_byte(self):
        ref = "うぞ〜😏🙌"
        assert yjson.loads(yjson.dumps(ref)) == ref

    def test_str_ascii_control(self):
        """
        worst case format_escaped_str_with_escapes() allocation
        """
        ref = "\x01\x1f" * 1024 * 16
        assert yjson.loads(yjson.dumps(ref)) == ref
        assert yjson.loads(yjson.dumps(ref, option=yjson.OPT_INDENT_2)) == ref

    def test_str_escape_quote_0(self):
        assert yjson.dumps('"aaaaaaabb') == b'"\\"aaaaaaabb"'

    def test_str_escape_quote_1(self):
        assert yjson.dumps('a"aaaaaabb') == b'"a\\"aaaaaabb"'

    def test_str_escape_quote_2(self):
        assert yjson.dumps('aa"aaaaabb') == b'"aa\\"aaaaabb"'

    def test_str_escape_quote_3(self):
        assert yjson.dumps('aaa"aaaabb') == b'"aaa\\"aaaabb"'

    def test_str_escape_quote_4(self):
        assert yjson.dumps('aaaa"aaabb') == b'"aaaa\\"aaabb"'

    def test_str_escape_quote_5(self):
        assert yjson.dumps('aaaaa"aabb') == b'"aaaaa\\"aabb"'

    def test_str_escape_quote_6(self):
        assert yjson.dumps('aaaaaa"abb') == b'"aaaaaa\\"abb"'

    def test_str_escape_quote_7(self):
        assert yjson.dumps('aaaaaaa"bb') == b'"aaaaaaa\\"bb"'

    def test_str_escape_quote_8(self):
        assert yjson.dumps('aaaaaaaab"') == b'"aaaaaaaab\\""'

    def test_str_escape_quote_multi(self):
        assert (
            yjson.dumps('aa"aaaaabbbbbbbbbbbbbbbbbbbb"bb')
            == b'"aa\\"aaaaabbbbbbbbbbbbbbbbbbbb\\"bb"'
        )

    def test_str_escape_quote_buffer(self):
        yjson.dumps(['"' * 4096] * 1024)

    def test_str_escape_backslash_0(self):
        assert yjson.dumps("\\aaaaaaabb") == b'"\\\\aaaaaaabb"'

    def test_str_escape_backslash_1(self):
        assert yjson.dumps("a\\aaaaaabb") == b'"a\\\\aaaaaabb"'

    def test_str_escape_backslash_2(self):
        assert yjson.dumps("aa\\aaaaabb") == b'"aa\\\\aaaaabb"'

    def test_str_escape_backslash_3(self):
        assert yjson.dumps("aaa\\aaaabb") == b'"aaa\\\\aaaabb"'

    def test_str_escape_backslash_4(self):
        assert yjson.dumps("aaaa\\aaabb") == b'"aaaa\\\\aaabb"'

    def test_str_escape_backslash_5(self):
        assert yjson.dumps("aaaaa\\aabb") == b'"aaaaa\\\\aabb"'

    def test_str_escape_backslash_6(self):
        assert yjson.dumps("aaaaaa\\abb") == b'"aaaaaa\\\\abb"'

    def test_str_escape_backslash_7(self):
        assert yjson.dumps("aaaaaaa\\bb") == b'"aaaaaaa\\\\bb"'

    def test_str_escape_backslash_8(self):
        assert yjson.dumps("aaaaaaaab\\") == b'"aaaaaaaab\\\\"'

    def test_str_escape_backslash_multi(self):
        assert (
            yjson.dumps("aa\\aaaaabbbbbbbbbbbbbbbbbbbb\\bb")
            == b'"aa\\\\aaaaabbbbbbbbbbbbbbbbbbbb\\\\bb"'
        )

    def test_str_escape_backslash_buffer(self):
        yjson.dumps(["\\" * 4096] * 1024)

    def test_str_escape_x32_0(self):
        assert yjson.dumps("\taaaaaaabb") == b'"\\taaaaaaabb"'

    def test_str_escape_x32_1(self):
        assert yjson.dumps("a\taaaaaabb") == b'"a\\taaaaaabb"'

    def test_str_escape_x32_2(self):
        assert yjson.dumps("aa\taaaaabb") == b'"aa\\taaaaabb"'

    def test_str_escape_x32_3(self):
        assert yjson.dumps("aaa\taaaabb") == b'"aaa\\taaaabb"'

    def test_str_escape_x32_4(self):
        assert yjson.dumps("aaaa\taaabb") == b'"aaaa\\taaabb"'

    def test_str_escape_x32_5(self):
        assert yjson.dumps("aaaaa\taabb") == b'"aaaaa\\taabb"'

    def test_str_escape_x32_6(self):
        assert yjson.dumps("aaaaaa\tabb") == b'"aaaaaa\\tabb"'

    def test_str_escape_x32_7(self):
        assert yjson.dumps("aaaaaaa\tbb") == b'"aaaaaaa\\tbb"'

    def test_str_escape_x32_8(self):
        assert yjson.dumps("aaaaaaaab\t") == b'"aaaaaaaab\\t"'

    def test_str_escape_x32_multi(self):
        assert (
            yjson.dumps("aa\taaaaabbbbbbbbbbbbbbbbbbbb\tbb")
            == b'"aa\\taaaaabbbbbbbbbbbbbbbbbbbb\\tbb"'
        )

    def test_str_escape_x32_buffer(self):
        yjson.dumps(["\t" * 4096] * 1024)

    def test_str_emoji(self):
        ref = "®️"
        assert yjson.loads(yjson.dumps(ref)) == ref

    def test_str_emoji_escape(self):
        ref = '/"®️/"'
        assert yjson.loads(yjson.dumps(ref)) == ref

    def test_very_long_list(self):
        yjson.dumps([[]] * 1024 * 16)

    def test_very_long_list_pretty(self):
        yjson.dumps([[]] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_very_long_dict(self):
        yjson.dumps([{}] * 1024 * 16)

    def test_very_long_dict_pretty(self):
        yjson.dumps([{}] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_very_long_str_empty(self):
        yjson.dumps([""] * 1024 * 16)

    def test_very_long_str_empty_pretty(self):
        yjson.dumps([""] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_very_long_str_not_empty(self):
        yjson.dumps(["a"] * 1024 * 16)

    def test_very_long_str_not_empty_pretty(self):
        yjson.dumps(["a"] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_very_long_bool(self):
        yjson.dumps([True] * 1024 * 16)

    def test_very_long_bool_pretty(self):
        yjson.dumps([True] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_very_long_int(self):
        yjson.dumps([(2**64) - 1] * 1024 * 16)

    def test_very_long_int_pretty(self):
        yjson.dumps([(2**64) - 1] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_very_long_float(self):
        yjson.dumps([sys.float_info.max] * 1024 * 16)

    def test_very_long_float_pretty(self):
        yjson.dumps([sys.float_info.max] * 1024 * 16, option=yjson.OPT_INDENT_2)

    def test_str_surrogates_loads(self):
        """
        str unicode surrogates loads()
        """
        pytest.raises(yjson.JSONDecodeError, yjson.loads, '"\ud800"')
        pytest.raises(yjson.JSONDecodeError, yjson.loads, '"\ud83d\ude80"')
        pytest.raises(yjson.JSONDecodeError, yjson.loads, '"\udcff"')
        pytest.raises(
            yjson.JSONDecodeError,
            yjson.loads,
            b'"\xed\xa0\xbd\xed\xba\x80"',
        )  # \ud83d\ude80

    def test_str_surrogates_dumps(self):
        """
        str unicode surrogates dumps()
        """
        pytest.raises(yjson.JSONEncodeError, yjson.dumps, "\ud800")
        pytest.raises(yjson.JSONEncodeError, yjson.dumps, "\ud83d\ude80")
        pytest.raises(yjson.JSONEncodeError, yjson.dumps, "\udcff")
        pytest.raises(yjson.JSONEncodeError, yjson.dumps, {"\ud83d\ude80": None})
        pytest.raises(
            yjson.JSONEncodeError,
            yjson.dumps,
            b"\xed\xa0\xbd\xed\xba\x80",
        )  # \ud83d\ude80

    def test_bytes_dumps(self):
        """
        bytes dumps not supported
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps([b"a"])

    def test_bytes_loads(self):
        """
        bytes loads
        """
        assert yjson.loads(b"[]") == []

    @pytest.mark.skipif(SUPPORTS_BYTEARRAY is False, reason="bytearray")
    def test_bytearray_loads(self):
        """
        bytearray loads
        """
        arr = bytearray()
        arr.extend(b"[]")
        assert yjson.loads(arr) == []

    @pytest.mark.skipif(SUPPORTS_MEMORYVIEW is False, reason="memoryview")
    def test_memoryview_loads_supported(self):
        """
        memoryview loads supported
        """
        assert yjson.loads(memoryview(b"[]")) == []

    @pytest.mark.skipif(SUPPORTS_MEMORYVIEW is True, reason="memoryview")
    def test_memoryview_loads_unsupported(self):
        """
        memoryview loads unsupported
        """
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads(memoryview(b"[]"))

    @pytest.mark.skipif(SUPPORTS_BYTEARRAY is False, reason="bytearray")
    def test_bytesio_loads_supported(self):
        """
        BytesIO loads supported
        """
        arr = io.BytesIO(b"[]")
        assert yjson.loads(arr.getbuffer()) == []

    @pytest.mark.skipif(SUPPORTS_BYTEARRAY is True, reason="bytearray")
    def test_bytesio_loads_unsupported(self):
        """
        BytesIO loads unsupported
        """
        arr = io.BytesIO(b"[]")
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads(arr.getbuffer())

    def test_bool(self):
        """
        bool
        """
        for obj, ref in ((True, "true"), (False, "false")):
            assert yjson.dumps(obj) == ref.encode("utf-8")
            assert yjson.loads(ref) == obj

    def test_bool_true_array(self):
        """
        bool true array
        """
        obj = [True] * 256
        ref = ("[" + ("true," * 255) + "true]").encode("utf-8")
        assert yjson.dumps(obj) == ref
        assert yjson.loads(ref) == obj

    def test_bool_false_array(self):
        """
        bool false array
        """
        obj = [False] * 256
        ref = ("[" + ("false," * 255) + "false]").encode("utf-8")
        assert yjson.dumps(obj) == ref
        assert yjson.loads(ref) == obj

    def test_none(self):
        """
        null
        """
        obj = None
        ref = "null"
        assert yjson.dumps(obj) == ref.encode("utf-8")
        assert yjson.loads(ref) == obj

    def test_int(self):
        """
        int compact and non-compact
        """
        obj = [-5000, -1000, -10, -5, -2, -1, 0, 1, 2, 5, 10, 1000, 50000]
        ref = b"[-5000,-1000,-10,-5,-2,-1,0,1,2,5,10,1000,50000]"
        assert yjson.dumps(obj) == ref
        assert yjson.loads(ref) == obj

    def test_null_array(self):
        """
        null array
        """
        obj = [None] * 256
        ref = ("[" + ("null," * 255) + "null]").encode("utf-8")
        assert yjson.dumps(obj) == ref
        assert yjson.loads(ref) == obj

    def test_nan_dumps(self):
        """
        NaN serializes to null
        """
        assert yjson.dumps(float("NaN")) == b"null"

    def test_nan_loads(self):
        """
        NaN is not valid JSON
        """
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads("[NaN]")
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads("[nan]")

    def test_infinity_dumps(self):
        """
        Infinity serializes to null
        """
        assert yjson.dumps(float("Infinity")) == b"null"

    def test_infinity_loads(self):
        """
        Infinity, -Infinity is not valid JSON
        """
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads("[infinity]")
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads("[Infinity]")
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads("[-Infinity]")
        with pytest.raises(yjson.JSONDecodeError):
            yjson.loads("[-infinity]")

    def test_int_53(self):
        """
        int 53-bit
        """
        for val in (9007199254740991, -9007199254740991):
            assert yjson.loads(str(val)) == val
            assert yjson.dumps(val, option=yjson.OPT_STRICT_INTEGER) == str(
                val,
            ).encode("utf-8")

    def test_int_53_exc(self):
        """
        int 53-bit exception on 64-bit
        """
        for val in (9007199254740992, -9007199254740992):
            with pytest.raises(yjson.JSONEncodeError):
                yjson.dumps(val, option=yjson.OPT_STRICT_INTEGER)

    def test_int_53_exc_usize(self):
        """
        int 53-bit exception on 64-bit usize
        """
        for val in (9223372036854775808, 18446744073709551615):
            with pytest.raises(yjson.JSONEncodeError):
                yjson.dumps(val, option=yjson.OPT_STRICT_INTEGER)

    def test_int_53_exc_128(self):
        """
        int 53-bit exception on 128-bit
        """
        val = 2**65
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(val, option=yjson.OPT_STRICT_INTEGER)

    def test_int_64(self):
        """
        int 64-bit
        """
        for val in (9223372036854775807, -9223372036854775807):
            assert yjson.loads(str(val)) == val
            assert yjson.dumps(val) == str(val).encode("utf-8")

    def test_uint_64(self):
        """
        uint 64-bit
        """
        for val in (0, 9223372036854775808, 18446744073709551615):
            assert yjson.loads(str(val)) == val
            assert yjson.dumps(val) == str(val).encode("utf-8")

    def test_int_128(self):
        """
        int 128-bit
        """
        for val in (18446744073709551616, -9223372036854775809):
            pytest.raises(yjson.JSONEncodeError, yjson.dumps, val)

    def test_float(self):
        """
        float
        """
        assert -1.1234567893 == yjson.loads("-1.1234567893")
        assert -1.234567893 == yjson.loads("-1.234567893")
        assert -1.34567893 == yjson.loads("-1.34567893")
        assert -1.4567893 == yjson.loads("-1.4567893")
        assert -1.567893 == yjson.loads("-1.567893")
        assert -1.67893 == yjson.loads("-1.67893")
        assert -1.7893 == yjson.loads("-1.7893")
        assert -1.893 == yjson.loads("-1.893")
        assert -1.3 == yjson.loads("-1.3")

        assert 1.1234567893 == yjson.loads("1.1234567893")
        assert 1.234567893 == yjson.loads("1.234567893")
        assert 1.34567893 == yjson.loads("1.34567893")
        assert 1.4567893 == yjson.loads("1.4567893")
        assert 1.567893 == yjson.loads("1.567893")
        assert 1.67893 == yjson.loads("1.67893")
        assert 1.7893 == yjson.loads("1.7893")
        assert 1.893 == yjson.loads("1.893")
        assert 1.3 == yjson.loads("1.3")

    def test_float_precision_loads(self):
        """
        float precision loads()
        """
        assert yjson.loads("31.245270191439438") == 31.245270191439438
        assert yjson.loads("-31.245270191439438") == -31.245270191439438
        assert yjson.loads("121.48791951161945") == 121.48791951161945
        assert yjson.loads("-121.48791951161945") == -121.48791951161945
        assert yjson.loads("100.78399658203125") == 100.78399658203125
        assert yjson.loads("-100.78399658203125") == -100.78399658203125

    def test_float_precision_dumps(self):
        """
        float precision dumps()
        """
        assert yjson.dumps(31.245270191439438) == b"31.245270191439438"
        assert yjson.dumps(-31.245270191439438) == b"-31.245270191439438"
        assert yjson.dumps(121.48791951161945) == b"121.48791951161945"
        assert yjson.dumps(-121.48791951161945) == b"-121.48791951161945"
        assert yjson.dumps(100.78399658203125) == b"100.78399658203125"
        assert yjson.dumps(-100.78399658203125) == b"-100.78399658203125"

    def test_float_edge(self):
        """
        float edge cases
        """
        assert yjson.dumps(0.8701) == b"0.8701"

        assert yjson.loads("0.8701") == 0.8701
        assert (
            yjson.loads("0.0000000000000000000000000000000000000000000000000123e50")
            == 1.23
        )
        assert yjson.loads("0.4e5") == 40000.0
        assert yjson.loads("0.00e-00") == 0.0
        assert yjson.loads("0.4e-001") == 0.04
        assert yjson.loads("0.123456789e-12") == 1.23456789e-13
        assert yjson.loads("1.234567890E+34") == 1.23456789e34
        assert yjson.loads("23456789012E66") == 2.3456789012e76

    def test_float_notation(self):
        """
        float notation
        """
        for val in ("1.337E40", "1.337e+40", "1337e40", "1.337E-4"):
            obj = yjson.loads(val)
            assert obj == float(val)
            assert yjson.dumps(val) == (f'"{val}"').encode("utf-8")

    def test_list(self):
        """
        list
        """
        obj = ["a", "😊", True, {"b": 1.1}, 2]
        ref = '["a","😊",true,{"b":1.1},2]'
        assert yjson.dumps(obj) == ref.encode("utf-8")
        assert yjson.loads(ref) == obj

    def test_tuple(self):
        """
        tuple
        """
        obj = ("a", "😊", True, {"b": 1.1}, 2)
        ref = '["a","😊",true,{"b":1.1},2]'
        assert yjson.dumps(obj) == ref.encode("utf-8")
        assert yjson.loads(ref) == list(obj)

    def test_object(self):
        """
        object() dumps()
        """
        with pytest.raises(yjson.JSONEncodeError):
            yjson.dumps(object())

"""Regression checks for the production Mojo decoder fast paths."""
import gc
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import yjson
import orjson

class TestDecoder(unittest.TestCase):
    def assert_same(self, text):
        self.assert_values_same(yjson.loads(text), orjson.loads(text))

    def assert_values_same(self, actual, expected):
        self.assertIs(type(actual), type(expected))
        self.assertEqual(actual, expected)
        if isinstance(expected, float):
            self.assertEqual(struct.pack("<d", actual), struct.pack("<d", expected))
        elif isinstance(expected, list):
            for value, reference in zip(actual, expected):
                self.assert_values_same(value, reference)
        elif isinstance(expected, dict):
            self.assertEqual(list(actual), list(expected))
            for key in expected:
                self.assert_values_same(actual[key], expected[key])

    def test_fraction_word_boundaries(self):
        # Short tails, the 8-digit word, the 19-digit fast limit and its fallback.
        for count in range(1, 41):
            fraction = ("1234567890" * 4)[:count]
            for sign in ("", "-"):
                for integer in ("0", "1", "1234567890123456789"):
                    for exponent in ("", "e-308", "e20"):
                        text = f"{sign}{integer}.{fraction}{exponent}"
                        with self.subTest(text=text):
                            self.assert_same(text)
                            self.assert_same(f"[{text},0]")
                            self.assert_same(f'{{"n":{text}}}')

    def test_numeric_arrays_and_mixed_slow_paths(self):
        values = [0, -1, 1.234567891, -0.0, 2**64 - 1, -(2**63), "x", {"k": 1}, [2.0], None]
        text = b'[18446744073709551616,1.23456789012345678901,' + orjson.dumps(values)[1:]
        self.assert_same(text)
        for count in (127, 128, 129, 255, 256, 257, 1025):
            with self.subTest(count=count):
                self.assert_same(orjson.dumps(values * count))
        for text in ("-0.0", "-0.000000000", "-1e-400"):
            self.assert_same(text)

    def test_float_conversion_boundaries(self):
        for mantissa in ("0", "1", "9007199254740991", "9007199254740992",
                         "9007199254740993", "1844674407370955161"):
            for exponent in (-400, -343, -342, -308, -23, -22, 0, 22, 23, 280):
                for sign in ("", "-"):
                    text = f"{sign}{mantissa}e{exponent}"
                    with self.subTest(text=text):
                        self.assert_same(text)
                        self.assert_same(f"[{text},0]")
                        self.assert_same(f'{{"n":{text}}}')
        for magnitude in ("4.9406564584124654e-324", "2.4703282292062327e-324",
                          "2.4703282292062328e-324", "2.2250738585072011e-308",
                          "2.2250738585072012e-308", "2.2250738585072014e-308",
                          "1.7976931348623157e308"):
            for sign in ("", "-"):
                text = sign + magnitude
                with self.subTest(text=text):
                    self.assert_same(text)
                    self.assert_same(f"[{text},0]")
                    self.assert_same(f'{{"n":{text}}}')
        for text in ("1.7976931348623159e308", "-1.7976931348623159e308"):
            with self.assertRaises(ValueError):
                yjson.loads(text)

    def test_small_nested_lists(self):
        # Item ownership and stack offsets across direct stores and bulk copies.
        values = [None, True, -0.0, 2**63, "text", {"k": [1, 2]}, [3, 4], False]
        for length in range(9):
            for prefix in range(5):
                data = ["prefix"] * prefix + [values[:length], values[:length]]
                with self.subTest(length=length, prefix=prefix):
                    self.assert_same(orjson.dumps(data))
        self.assert_same(orjson.dumps([values[:length] for length in range(9)] * 64))

    def test_cached_key_word_boundaries(self):
        for length in (0, 1, 7, 8, 9, 15, 16, 17, 63, 64, 65):
            key = ("abcdefghijklmnop" * 5)[:length]
            keys = [key]
            for position in {0, length // 2, length - 1}:
                if 0 <= position < length:
                    keys.append(key[:position] + "Z" + key[position + 1:])
            for order in (keys, keys[::-1], keys[1:] + keys[:1]):
                document = {name: [index, {"nested": name}] for index, name in enumerate(order)}
                for _ in range(5):
                    with self.subTest(length=length, order=order):
                        self.assert_same(orjson.dumps(document))

    def test_error_cleanup_and_subsequent_calls(self):
        for text in ("[1,]", "[1 2]", "[1e400]", "[1.123456789e+]", "[1.123456789x]", "[1,01]", "[1,-]", "[1,{}", '[1,{"x":2},3,]'):
            for _ in range(5):
                with self.subTest(text=text), self.assertRaises(ValueError):
                    yjson.loads(text)
                self.assert_same('[1.123456789,{"x":2},3]')
        with self.assertRaises(ValueError):
            yjson.loads("[" * 1025 + "]" * 1025)
        self.assert_same("[" * 1024 + "]" * 1024)

    def test_input_variants(self):
        data = b'{"name":"caf\\u00e9","value":1.123456789}'
        for value in (data, data.decode(), bytearray(data), memoryview(data)):
            self.assert_same(value)
        for value in (memoryview(data)[::2], 42, '"\ud800"'):
            with self.assertRaises(ValueError):
                yjson.loads(value)

    def test_whitespace(self):
        for byte in range(256):
            document = b"[1.123456789, 2]"
            for data in (bytes([byte]) + document, document + bytes([byte])):
                with self.subTest(data=data):
                    if byte in (9, 10, 13, 32):
                        self.assert_same(data)
                    else:
                        with self.assertRaises(ValueError):
                            yjson.loads(data)

    def test_whitespace_run_boundaries(self):
        for length in (0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65):
            for padding in (b" " * length, (b" \t\r\n" * 17)[:length]):
                self.assert_same(padding + b'[{},{}]' + padding)
                self.assert_same(b'[{}' + padding + b',{}]')
                for byte in range(256):
                    data = b'[{}' + padding + bytes([byte]) + b' ,{}]'
                    with self.subTest(padding=padding, byte=byte):
                        if byte in (9, 10, 13, 32):
                            self.assert_same(data)
                        else:
                            with self.assertRaises(ValueError):
                                yjson.loads(data)


if __name__ == "__main__":
    sys.setrecursionlimit(20000)
    unittest.main()

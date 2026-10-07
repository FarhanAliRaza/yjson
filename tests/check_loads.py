"""Checks loads() against orjson.loads and Python's float().

    python tests/check_loads.py [build/jsonexamples]

Every input must either decode to the same value, with the same types, as orjson, or
make both raise. Inputs: the corpus documents and the suite's fixtures, as str, bytes,
bytearray and memoryview; random numbers at the edges of the integer and double
ranges; random strings with escapes, surrogates and invalid UTF-8; mutated documents;
and the number text of random doubles and of exact halfway points between adjacent
doubles, compared with float(). Exits non-zero on the first kind of difference.
"""
from fractions import Fraction
import lzma
import math
from pathlib import Path
import random
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import orjson
import yjson

ROOT = Path(__file__).resolve().parents[1]
sys.setrecursionlimit(20000)  # the fixtures nest 1,024 deep
rng = random.Random(20251007)
failures = 0
checked = 0


def same(a, b):
    if type(a) is not type(b):
        return False
    if isinstance(a, float):
        return struct.pack("<d", a) == struct.pack("<d", b)
    if isinstance(a, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict):
        return list(a) == list(b) and all(same(a[k], b[k]) for k in a)
    return a == b


def outcome(fn, data):
    try:
        return True, fn(data)
    except yjson.JSONDecodeError:
        return False, None
    except orjson.JSONDecodeError:
        return False, None


def report(kind, data, *details):
    global failures
    failures += 1
    if failures <= 20:
        print(kind, repr(data)[:100], *[repr(d)[:80] for d in details])


def check(data):
    """yjson and orjson both decode `data` to the same value, or both reject it."""
    global checked
    checked += 1
    ok_y, value_y = outcome(yjson.loads, data)
    ok_o, value_o = outcome(orjson.loads, data)
    if ok_y != ok_o:
        report("one rejected:", data, "yjson" if ok_y else "orjson", "accepted")
    elif ok_y and not same(value_y, value_o):
        report("values differ:", data, value_y, value_o)


def check_float(text):
    """yjson decodes the number text as float() does, or raises on an infinite value."""
    global checked
    checked += 1
    expected = float(text)
    try:
        got = yjson.loads(text)
    except yjson.JSONDecodeError:
        if not math.isinf(expected):
            report("rejected:", text, expected)
        return
    if math.isinf(expected):
        report("accepted infinite:", text, got)
    elif not same(got, expected):
        report("float differs:", text, got, expected)


def documents():
    paths = sorted((ROOT / "tests" / "suite" / "data").rglob("*.json*"))
    if len(sys.argv) > 1:
        paths += sorted(Path(sys.argv[1]).glob("*.json"))
    for path in paths:
        data = path.read_bytes()
        yield path.name, lzma.decompress(data) if path.suffix == ".xz" else data


def random_double():
    while True:
        value = struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64)))[0]
        if math.isfinite(value):
            return value


def halfway_texts():
    """The decimal expansion of the midpoint between two adjacent doubles, to 19 digits, and its neighbours."""
    value = struct.unpack("<d", struct.pack("<Q", rng.getrandbits(52) | rng.randrange(1, 0x7FE) << 52))[0]
    half = (Fraction(value) + Fraction(math.nextafter(value, math.inf))) / 2
    numerator, denominator, exponent = half.numerator, half.denominator, 0
    while numerator // denominator >= 10**19:
        denominator *= 10
        exponent += 1
    while numerator // denominator < 10**18:
        numerator *= 10
        exponent -= 1
    digits = numerator // denominator
    return [f"{m}e{exponent}" for m in (digits - 1, digits, digits + 1)]


def main():
    docs = list(documents())
    for name, data in docs:
        for variant in (data, bytearray(data), memoryview(data)):
            check(variant)
        try:
            check(data.decode("utf-8"))
        except UnicodeDecodeError:
            pass
    print(f"documents: {len(docs)}")

    for _ in range(60000):
        kind = rng.randrange(6)
        if kind == 0:
            text = repr(random_double())
        elif kind == 1:
            text = f"{rng.uniform(-1e6, 1e6):.{rng.randrange(0, 20)}f}"
        elif kind == 2:
            text = f"{rng.randrange(0, 10 ** rng.randrange(1, 25))}e{rng.choice(['', '+', '-'])}{rng.randrange(0, 400)}"
        elif kind == 3:
            text = str(rng.randrange(-(2**64) - 5, 2**64 + 5))
        elif kind == 4:
            text = "0." + "0" * rng.randrange(0, 40) + str(rng.randrange(1, 10**20)) + (f"e{rng.randrange(-50, 50)}" if rng.random() < 0.5 else "")
        else:
            text = str(rng.choice([-(2**63), -(2**63) - 1, 2**63, 2**64 - 1, 2**64, 2**63 - 1, 0]))
        check(text)
        check(f"[{text}]")

    for _ in range(200000):
        kind = rng.randrange(5)
        if kind == 0:
            check_float(repr(random_double()))
        elif kind == 1:
            digits = rng.randrange(1, 20)
            check_float(f"{rng.randrange(10 ** (digits - 1), 10 ** digits)}e{rng.randrange(-360, 320)}")
        elif kind == 2:
            for text in halfway_texts():
                check_float(text)
        elif kind == 3:
            check_float(f"{rng.randrange(1, 10 ** rng.randrange(1, 20))}e{rng.choice([rng.randrange(-345, -300), rng.randrange(290, 330)])}")
        else:
            digits = rng.randrange(20, 40)
            check_float(f"{rng.randrange(10 ** (digits - 1), 10 ** digits)}e{rng.randrange(-350, 310)}")
    for text in ["1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308", "2.2250738585072011e-308",
                 "2.2250738585072014e-308", "4.9406564584124654e-324", "2.4703282292062327e-324", "2.4703282292062328e-324",
                 "1e-324", "9007199254740993.0", "9007199254740992.5", "1e23", "8.98846567431158e307", "0.1", "7.038531e-26",
                 "1.2345678901234567e-310", "1e-7", "5e-324", "1e308", "2e308", "2.5e-324", "4503599627370497.5", "0.0000000000000000000000000000000000000000000000000123e50"]:
        check_float(text)
    print("numbers done")

    for _ in range(20000):
        parts = []
        for _ in range(rng.randrange(0, 12)):
            kind = rng.randrange(8)
            if kind == 0:
                parts.append(chr(rng.randrange(0x20, 0x7F)))
            elif kind == 1:
                parts.append(chr(rng.randrange(0x80, 0x800)))
            elif kind == 2:
                parts.append(chr(rng.randrange(0x800, 0xD800)))
            elif kind == 3:
                parts.append(chr(rng.randrange(0x10000, 0x110000)))
            elif kind == 4:
                parts.append("\\" + rng.choice('"\\/bfnrtu'))
            elif kind == 5:
                parts.append("\\u%04x" % rng.randrange(0, 0x10000))
            elif kind == 6:
                parts.append("\\u%04X\\u%04X" % (rng.randrange(0xD800, 0xDC00), rng.randrange(0xDC00, 0xE000)))
            else:
                parts.append(chr(rng.randrange(0, 0x20)))
        body = "".join(parts)
        check(f'"{body}"')
        check(f'"{body}"'.encode("utf-8", "surrogatepass"))
        check('{"k' + body + '": 1}')
    for _ in range(10000):
        raw = bytes(rng.randrange(0, 256) for _ in range(rng.randrange(1, 6)))
        check(b'"' + raw + b'"')
        check(b'["a", "' + raw + b'", 1]')
    print("strings done")

    small = [data for _, data in docs if len(data) < 4000] or [data for _, data in docs[:3]]
    for _ in range(20000):
        data = bytearray(rng.choice(small))
        kind = rng.randrange(4)
        if kind == 0 and data:
            del data[rng.randrange(len(data)):]
        elif kind == 1 and data:
            data[rng.randrange(len(data))] = rng.randrange(256)
        elif kind == 2:
            data.insert(rng.randrange(len(data) + 1), rng.choice(b'[]{},:"\\ 0123456789.eE+-tfnu\n\t\x00\xff'))
        else:
            data = data[:rng.randrange(len(data) + 1)] + data[rng.randrange(len(data) + 1):]
        check(bytes(data))
    print("mutations done")

    for text in ["", " ", "[", '{"a":}', "nul", "[1,]", "{,}", "{}{}", "01", "-", "-0", "-0.0", "1.", ".5", "1e", "1e+", "[1 2]", '{"a" 1}',
                 "[}", "tru", "truee", "\x00", "[1]\x00", '"abc', '"\\x"', '"\\u12"', '"\\uD800\\u0041"', '"\\uDC00"', "123abc", "1e400",
                 "-1e400", "[" * 1024 + "]" * 1024, "[" * 1025 + "]" * 1025, '{"key":' * 1024 + '{"key":true}' + "}" * 1024, "[" * 100000,
                 '"\ud800"', '"\udcff"', b'"\xed\xa0\xbd\xed\xba\x80"', b"\xef\xbb\xbf{}", b"\xff", b'"\xc8\x93', b'"\xc8',
                 "\t\n\r [true]\t\n\r ", '{"a":1,"a":2,"b":3}', '{"":""}', "[[]]", "{}", "[]", '""', "9007199254740993", "18446744073709551615"]:
        check(text)
    check(memoryview(b"abcd")[::2])
    check(42)
    print(f"checked {checked}, differences {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

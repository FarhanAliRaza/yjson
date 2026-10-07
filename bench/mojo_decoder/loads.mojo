# The loads() parser ported to Mojo, for a like-for-like comparison with src/decoder.c.
# Not part of the package: the C decoder is the one shipped. See README.md here.
#
# Same algorithm as the C version: SSE scans, key cache with next-key prediction,
# unrolled digit chain, Clinger + Eisel-Lemire floats, lists built from a value
# stack. Untyped decoding only. CPython object layouts come from -D defines that
# build.sh takes from the main build's layout probe (defaults: CPython 3.13).
from std.ffi import external_call
from std.memory import alloc, unsafe_stack_allocation
from std.bit import count_trailing_zeros, count_leading_zeros
from std.simd import pack_bits
from std.sys.intrinsics import likely, unlikely
from std.sys import get_defined_int
from std.memory import bitcast
from std.collections import Array

comptime P8 = Pointer[UInt8, MutUntrackedOrigin]
comptime PI = Pointer[Int, MutUntrackedOrigin]
comptime P64 = Pointer[UInt64, MutUntrackedOrigin]
comptime PF = Pointer[Float64, MutUntrackedOrigin]
comptime MAX_DEPTH = 1024
comptime SLOTS = 2048
comptime KEY_MAX = 64
comptime INLINE_STACK = 256
comptime LIST_ITEMS = get_defined_int["YJSON_LIST_ITEMS", 24]()
comptime STR_LENGTH = get_defined_int["YJSON_STR_LENGTH", 16]()
comptime STR_ASCII_DATA = get_defined_int["YJSON_STR_ASCII_DATA", 40]()

comptime ERR_EOF = 1
comptime ERR_VALUE = 2
comptime ERR_UTF8 = 3
comptime ERR_ESCAPE = 4
comptime ERR_CONTROL = 5
comptime ERR_SURROGATE = 6
comptime ERR_LEADING_ZERO = 7
comptime ERR_NUMBER = 8
comptime ERR_INF = 9
comptime ERR_DEPTH = 10
comptime ERR_COMMA = 11
comptime ERR_SEPARATOR = 12
comptime ERR_KEY = 13
comptime ERR_COLON = 14
comptime ERR_EMPTY = 15
comptime ERR_BOM = 16
comptime ERR_TRAILING = 17
comptime ERR_MEMORY = 18



struct Ctx:
    var key_cache: PI
    var next_slot: PI
    var last_slot: Int
    var py_true: Int
    var py_false: Int
    var py_none: Int
    var powers: P64
    var exact10: PF
    var pow10: P64
    var hex: P8
    var escapes: P8

    def __init__(out self, t: Int, f: Int, n: Int):
        self.key_cache = alloc[Int](SLOTS)
        self.next_slot = alloc[Int](SLOTS)
        for i in range(SLOTS):
            self.key_cache[unsafe_offset=i] = 0
            self.next_slot[unsafe_offset=i] = 0
        self.last_slot = -1
        self.py_true = t
        self.py_false = f
        self.py_none = n
        self.powers = P64(unsafe_from_address=external_call["yjson_mojo_powers_table", Int]())
        self.exact10 = PF(unsafe_from_address=Int(alloc[Float64](23)))
        self.pow10 = P64(unsafe_from_address=Int(alloc[UInt64](20)))
        var f10: Float64 = 1.0
        for i in range(23):
            self.exact10[unsafe_offset=i] = f10
            f10 *= 10.0
        var u10: UInt64 = 1
        for i in range(20):
            self.pow10[unsafe_offset=i] = u10
            u10 *= 10
        self.hex = P8(unsafe_from_address=Int(alloc[UInt8](256)))
        self.escapes = P8(unsafe_from_address=Int(alloc[UInt8](256)))
        for i in range(256):
            self.hex[unsafe_offset=i] = 0xFF
            self.escapes[unsafe_offset=i] = 0  # 0: invalid, 1: \u, else the byte the escape stands for
        for i in range(10):
            self.hex[unsafe_offset=48 + i] = UInt8(i)
        for i in range(6):
            self.hex[unsafe_offset=97 + i] = UInt8(10 + i)
            self.hex[unsafe_offset=65 + i] = UInt8(10 + i)
        self.escapes[unsafe_offset=34] = 34
        self.escapes[unsafe_offset=92] = 92
        self.escapes[unsafe_offset=47] = 47
        self.escapes[unsafe_offset=98] = 8
        self.escapes[unsafe_offset=102] = 12
        self.escapes[unsafe_offset=110] = 10
        self.escapes[unsafe_offset=114] = 13
        self.escapes[unsafe_offset=116] = 9
        self.escapes[unsafe_offset=117] = 1


struct Parser:
    var start: Int
    var cur: Int
    var end: Int
    var depth: Int
    var err: Int
    var err_at: Int
    var stack: PI
    var stack_len: Int
    var stack_cap: Int
    var stack_inline: Int
    var scratch: Int
    var scratch_cap: Int
    var ctx: Pointer[Ctx, MutUntrackedOrigin]

    def __init__(out self, ctx: Int, data: Int, length: Int, inline_stack: Int):
        self.start = data
        self.cur = data
        self.end = data + length
        self.depth = 0
        self.err = 0
        self.err_at = 0
        self.stack = PI(unsafe_from_address=inline_stack)
        self.stack_len = 0
        self.stack_cap = INLINE_STACK
        self.stack_inline = inline_stack
        self.scratch = 0
        self.scratch_cap = 0
        self.ctx = Pointer[Ctx, MutUntrackedOrigin](unsafe_from_address=ctx)




@always_inline
def rd(a: Int) -> UInt8:
    return P8(unsafe_from_address=a)[]


@always_inline
def fail[o_: Origin[mut=True]](p: Pointer[Parser, o_], code: Int, at: Int) -> Int:
    p[].err = code
    p[].err_at = at - p[].start
    return 0


@always_inline
def is_space(b: UInt8) -> Bool:
    # One bit test: bits 9, 10, 13 and 32 of the mask.
    return b <= 32 and ((UInt64(0x100002600) >> UInt64(b)) & 1) != 0


@always_inline
def is_digit(b: UInt8) -> Bool:
    return (b - 48) < 10


# ---- refcounts, as the C macros do them (3.12+ immortal objects have the sign bit of the low word set) ----
@always_inline
def incref(o: Int):
    var p = Pointer[Int64, MutUntrackedOrigin](unsafe_from_address=o)
    var v = p[]
    if (v & 0x80000000) != 0:
        return
    p[] = v + 1


@always_inline
def decref(o: Int):
    var p = Pointer[Int64, MutUntrackedOrigin](unsafe_from_address=o)
    var v = p[]
    if (v & 0x80000000) != 0:
        return
    v -= 1
    p[] = v
    if v == 0:
        external_call["_Py_Dealloc", NoneType](o)


# ---- scanning ----
@no_inline
def skip_space_run(c0: Int, end: Int) -> Int:
    var c = c0
    if c >= end or not is_space(rd(c)):
        return c
    c += 1
    if c >= end or not is_space(rd(c)):
        return c
    c += 1
    while end - c >= 16:
        var v = P8(unsafe_from_address=c).unsafe_load[width=16]()
        var white = v.eq(32) | v.eq(10) | v.eq(13) | v.eq(9)
        var mask = ~UInt16(pack_bits(white))
        if mask != 0:
            return c + Int(count_trailing_zeros(mask))
        c += 16
    while c < end and is_space(rd(c)):
        c += 1
    return c


@always_inline
def skip_space(c: Int, end: Int) -> Int:
    if c >= end or not is_space(rd(c)):
        return c
    return skip_space_run(c + 1, end)


# First byte that is '"', '\\' or a control character (and, when strict, any byte >= 0x80), or end.
@always_inline
def scan[strict: Bool](c0: Int, end: Int) -> Int:
    var c = c0
    while end - c >= 16:
        var v = P8(unsafe_from_address=c).unsafe_load[width=16]()
        var special: SIMD[DType.bool, 16]
        comptime if strict:
            special = v.eq(34) | v.eq(92) | v.cast[DType.int8]().lt(32)
        else:
            special = v.eq(34) | v.eq(92) | v.lt(32)
        var mask = UInt16(pack_bits(special))
        if mask != 0:
            return c + Int(count_trailing_zeros(mask))
        c += 16
    while c < end:
        var b = rd(c)
        if b == 34 or b == 92 or b < 32:
            break
        comptime if strict:
            if b >= 128:
                break
        c += 1
    return c


# ---- strings ----
@always_inline
def ascii_string(src: Int, n: Int) -> Int:
    var s = external_call["PyUnicode_New", Int](n, 127)
    if s != 0 and n != 0:
        external_call["memcpy", NoneType](s + STR_ASCII_DATA, src, n)
    return s


@always_inline
def hex_value(b: UInt8) -> Int:
    if is_digit(b):
        return Int(b - 48)
    var l = b | 0x20
    if l >= 97 and l <= 102:
        return Int(l - 97 + 10)
    return -1


# The four hex digits after "\u" at c (the backslash); -1 invalid, -2 truncated.
@always_inline
def hex4(hex: P8, c: Int, end: Int) -> Int:
    if c + 6 > end:
        return -2
    var a = Int(hex[unsafe_offset=Int(rd(c + 2))])
    var b = Int(hex[unsafe_offset=Int(rd(c + 3))])
    var d = Int(hex[unsafe_offset=Int(rd(c + 4))])
    var e = Int(hex[unsafe_offset=Int(rd(c + 5))])
    if (a | b | d | e) & 0xF0 != 0:
        return -1
    return (a << 12) | (b << 8) | (d << 4) | e


@always_inline
def put_utf8(out0: Int, cp: Int) -> Int:
    var out = P8(unsafe_from_address=out0)
    if cp < 0x80:
        out[] = UInt8(cp)
        return out0 + 1
    if cp < 0x800:
        out[] = UInt8(0xC0 | (cp >> 6))
        out[unsafe_offset=1] = UInt8(0x80 | (cp & 0x3F))
        return out0 + 2
    if cp < 0x10000:
        out[] = UInt8(0xE0 | (cp >> 12))
        out[unsafe_offset=1] = UInt8(0x80 | ((cp >> 6) & 0x3F))
        out[unsafe_offset=2] = UInt8(0x80 | (cp & 0x3F))
        return out0 + 3
    out[] = UInt8(0xF0 | (cp >> 18))
    out[unsafe_offset=1] = UInt8(0x80 | ((cp >> 12) & 0x3F))
    out[unsafe_offset=2] = UInt8(0x80 | ((cp >> 6) & 0x3F))
    out[unsafe_offset=3] = UInt8(0x80 | (cp & 0x3F))
    return out0 + 4


@always_inline
def decode_utf8[o_: Origin[mut=True]](p: Pointer[Parser, o_], src: Int, n: Int, quote: Int) -> Int:
    var s = external_call["PyUnicode_DecodeUTF8", Int](src, n, 0)
    if s == 0:
        external_call["PyErr_Clear", NoneType]()
        return fail(p, ERR_UTF8, quote)
    return s


@no_inline
def string_slow[o_: Origin[mut=True]](p: Pointer[Parser, o_], start: Int, c0: Int) -> Int:
    var end = p[].end
    var c = c0
    if p[].scratch_cap < end - start:
        var fresh = external_call["realloc", Int](p[].scratch, end - start)
        if fresh == 0:
            return fail(p, ERR_MEMORY, start)
        p[].scratch = fresh
        p[].scratch_cap = end - start
    ref ctx = p[].ctx[]
    var hex = ctx.hex
    var escapes = ctx.escapes
    var out = p[].scratch
    external_call["memcpy", NoneType](out, start, c - start)
    out += c - start
    while True:
        if c >= end:
            return fail(p, ERR_EOF, end)
        var b = rd(c)
        if b == 34:
            c += 1
            break
        if b == 92:
            if c + 1 >= end:
                return fail(p, ERR_EOF, end)
            var e = escapes[unsafe_offset=Int(rd(c + 1))]
            if e > 1:
                P8(unsafe_from_address=out)[] = e
                out += 1
                c += 2
            elif e == 1:
                var cp = hex4(hex, c, end)
                if cp == -2:
                    return fail(p, ERR_EOF, end)
                if cp == -1:
                    return fail(p, ERR_ESCAPE, c)
                if cp >= 0xD800 and cp <= 0xDBFF:
                    if c + 8 > end:
                        return fail(p, ERR_SURROGATE, c)
                    if rd(c + 6) != 92 or rd(c + 7) != 117:
                        return fail(p, ERR_SURROGATE, c)
                    var low = hex4(hex, c + 6, end)
                    if low == -2:
                        return fail(p, ERR_EOF, end)
                    if low < 0xDC00 or low > 0xDFFF:
                        return fail(p, ERR_SURROGATE, c)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00)
                    c += 12
                elif cp >= 0xDC00 and cp <= 0xDFFF:
                    return fail(p, ERR_SURROGATE, c)
                else:
                    c += 6
                out = put_utf8(out, cp)
            else:
                return fail(p, ERR_ESCAPE, c)
            continue
        if b < 32:
            return fail(p, ERR_CONTROL, c)
        var run = scan[False](c, end)
        external_call["memcpy", NoneType](out, c, run - c)
        out += run - c
        c = run
    var s = decode_utf8(p, p[].scratch, out - p[].scratch, start - 1)
    if s != 0:
        p[].cur = c
    return s


@always_inline
def string_from[o_: Origin[mut=True]](p: Pointer[Parser, o_], start: Int, special0: Int) -> Int:
    var end = p[].end
    var special = special0
    if special < end and rd(special) == 34:
        p[].cur = special + 1
        return ascii_string(start, special - start)
    if special < end and rd(special) >= 128:
        var close = scan[False](special, end)
        if close < end and rd(close) == 34:
            var s = decode_utf8(p, start, close - start, start - 1)
            if s != 0:
                p[].cur = close + 1
            return s
        special = close
    return string_slow(p, start, special)


@always_inline
def parse_string[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var start = p[].cur + 1
    return string_from(p, start, scan[True](start, p[].end))


@always_inline
def key_hash(s0: Int, n0: Int) -> UInt64:
    var s = s0
    var n = n0
    var h = UInt64(0x9E3779B97F4A7C15) ^ UInt64(n)
    while n >= 8:
        var w = P64(unsafe_from_address=s)[]
        h = (h ^ w) * UInt64(0xFF51AFD7ED558CCD)
        h ^= h >> 29
        s += 8
        n -= 8
    if n > 0:
        var w: UInt64 = 0
        for i in range(n):
            w |= UInt64(rd(s + i)) << UInt64(8 * i)
        h = (h ^ w) * UInt64(0xFF51AFD7ED558CCD)
        h ^= h >> 29
    return h ^ (h >> 32)


@always_inline
def key_matches(cached: Int, start: Int, n: Int) -> Bool:
    if cached == 0:
        return False
    if PI(unsafe_from_address=cached + STR_LENGTH)[] != n:
        return False
    return external_call["memcmp", Int32](cached + STR_ASCII_DATA, start, n) == 0


def parse_key[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var start = p[].cur + 1
    var end = p[].end
    var special = scan[True](start, end)
    var n = special - start
    if special >= end or rd(special) != 34 or n > KEY_MAX:
        return string_from(p, start, special)
    ref ctx = p[].ctx[]
    var index: Int
    var cached: Int
    if ctx.last_slot >= 0:
        index = ctx.next_slot[unsafe_offset=ctx.last_slot]
        cached = ctx.key_cache[unsafe_offset=index]
        if key_matches(cached, start, n):
            ctx.last_slot = index
            p[].cur = special + 1
            incref(cached)
            return cached
    index = Int(key_hash(start, n) & UInt64(SLOTS - 1))
    cached = ctx.key_cache[unsafe_offset=index]
    if key_matches(cached, start, n):
        incref(cached)
    else:
        cached = ascii_string(start, n)
        if cached == 0:
            return 0
        var old = ctx.key_cache[unsafe_offset=index]
        incref(cached)
        ctx.key_cache[unsafe_offset=index] = cached
        if old != 0:
            decref(old)
    if ctx.last_slot >= 0:
        ctx.next_slot[unsafe_offset=ctx.last_slot] = index
    ctx.last_slot = index
    p[].cur = special + 1
    return cached


# ---- numbers ----
@always_inline
def eight_digits_value(v0: UInt64) -> UInt64:
    var v = v0 - UInt64(0x3030303030303030)
    v = v * 10 + (v >> 8)
    v = ((v & UInt64(0x000000FF000000FF)) * (100 + (UInt64(1000000) << 32)) + (((v >> 16) & UInt64(0x000000FF000000FF)) * (1 + (UInt64(10000) << 32)))) >> 32
    return v & UInt64(0xFFFFFFFF)


@always_inline
def is_eight_digits(v: UInt64) -> Bool:
    return ((v & UInt64(0xF0F0F0F0F0F0F0F0)) | (((v + UInt64(0x0606060606060606)) & UInt64(0xF0F0F0F0F0F0F0F0)) >> 4)) == UInt64(0x3333333333333333)


@always_inline
def digits_value(c0: Int, n0: Int) -> UInt64:
    var c = c0
    var n = n0
    var value: UInt64 = 0
    while n >= 8:
        value = value * 100000000 + eight_digits_value(P64(unsafe_from_address=c)[])
        c += 8
        n -= 8
    while n > 0:
        value = value * 10 + UInt64(rd(c) - 48)
        c += 1
        n -= 1
    return value


@always_inline
def scan_digits(c0: Int, end: Int) -> Int:
    var c = c0
    while end - c >= 8 and is_eight_digits(P64(unsafe_from_address=c)[]):
        c += 8
    while c < end and is_digit(rd(c)):
        c += 1
    return c


@always_inline
def bits_to_double(bits: UInt64) -> Float64:
    return bitcast[DType.float64, 1](bits)


@always_inline
def double_to_bits(value: Float64) -> UInt64:
    return bitcast[DType.uint64, 1](value)


# Eisel-Lemire, as in decoder.c. Returns False only for an infinite result.
@always_inline
def eisel_lemire(powers: P64, mantissa: UInt64, exponent: Int, mut out: UInt64) -> Bool:
    if mantissa == 0 or exponent < -342:
        out = 0
        return True
    if exponent > 308:
        return False
    var lz = Int(count_leading_zeros(mantissa))
    var w = mantissa << UInt64(lz)
    var base = (exponent + 342) * 2
    var product = UInt128(w) * UInt128(powers[unsafe_offset=base])
    var high = UInt64(product >> 64)
    var low = UInt64(product)
    if (high & 0x1FF) == 0x1FF:
        var second_high = UInt64((UInt128(w) * UInt128(powers[unsafe_offset=base + 1])) >> 64)
        low += second_high
        if second_high > low:
            high += 1
    var upper_bit = Int(high >> 63)
    var shift = upper_bit + 64 - 52 - 3
    var m = high >> UInt64(shift)
    var power2 = (((152170 + 65536) * exponent) >> 16) + 63 + upper_bit - lz + 1023
    if power2 <= 0:
        if -power2 + 1 >= 64:
            out = 0
            return True
        m >>= UInt64(-power2 + 1)
        m += m & 1
        m >>= 1
        power2 = 0 if m < (UInt64(1) << 52) else 1
    else:
        if low <= 1 and exponent >= -4 and exponent <= 23 and (m & 3) == 1 and (m << UInt64(shift)) == high:
            m &= ~UInt64(1)
        m += m & 1
        m >>= 1
        if m >= (UInt64(2) << 52):
            m = UInt64(1) << 52
            power2 += 1
        m &= ~(UInt64(1) << 52)
        if power2 >= 0x7FF:
            return False
    out = m | (UInt64(power2) << 52)
    return True


@no_inline
def float_from_text[o_: Origin[mut=True]](p: Pointer[Parser, o_], start: Int, stop: Int) -> Int:
    var n = stop - start
    var buffer = unsafe_stack_allocation[64, UInt8]()
    var text = Int(buffer)
    var heap = 0
    if n >= 64:
        heap = external_call["malloc", Int](n + 1)
        if heap == 0:
            return fail(p, ERR_MEMORY, start)
        text = heap
    external_call["memcpy", NoneType](text, start, n)
    P8(unsafe_from_address=text)[unsafe_offset=n] = 0
    var tail = unsafe_stack_allocation[1, Int]()
    var value = external_call["PyOS_string_to_double", Float64](text, Int(tail), 0)
    if heap != 0:
        external_call["free", NoneType](heap)
    if value == -1.0 and external_call["PyErr_Occurred", Int]() != 0:
        return 0
    if (double_to_bits(value) & UInt64(0x7FF0000000000000)) == UInt64(0x7FF0000000000000):
        return fail(p, ERR_INF, start)
    return external_call["PyFloat_FromDouble", Int](value)


@always_inline
def make_int[o_: Origin[mut=True]](negative: Bool, mantissa: UInt64, p: Pointer[Parser, o_], start: Int, c: Int) -> Int:
    if not negative:
        if mantissa <= UInt64(0x7FFFFFFFFFFFFFFF):
            return external_call["PyLong_FromLongLong", Int](Int64(mantissa))
        return external_call["PyLong_FromUnsignedLongLong", Int](mantissa)
    if mantissa < (UInt64(1) << 63):
        return external_call["PyLong_FromLongLong", Int](-Int64(mantissa))
    if mantissa == (UInt64(1) << 63):
        return external_call["PyLong_FromLongLong", Int](Int64(-9223372036854775807) - 1)
    return float_from_text(p, start, c)


@always_inline
def make_float[o_: Origin[mut=True]](negative: Bool, mantissa: UInt64, exponent: Int, p: Pointer[Parser, o_], start: Int, c: Int) -> Int:
    ref ctx = p[].ctx[]
    var value: Float64
    if mantissa <= (UInt64(1) << 53) and exponent >= -22 and exponent <= 22:
        value = Float64(Int64(mantissa))  # below 2^53 here: a signed conversion is one instruction
        if exponent < 0:
            value = value / ctx.exact10[unsafe_offset=-exponent]
        else:
            value = value * ctx.exact10[unsafe_offset=exponent]
        return external_call["PyFloat_FromDouble", Int](-value if negative else value)
    var bits: UInt64 = 0
    if eisel_lemire(ctx.powers, mantissa, exponent, bits):
        value = bits_to_double(bits)
        return external_call["PyFloat_FromDouble", Int](-value if negative else value)
    return float_from_text(p, start, c)


# Numbers of any length: scanned first, converted after. Reached with 20 or more digits.
@no_inline
def parse_number_slow[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var start = p[].cur
    var c = start
    var end = p[].end
    var negative = rd(c) == 45
    if negative:
        c += 1
    var int_start = c
    if rd(c) == 48:
        c += 1
    else:
        c = scan_digits(c + 1, end)
    var int_end = c
    var frac_start = c
    var frac_end = c
    var is_float = False
    var exponent = 0
    if rd(c) == 46:
        is_float = True
        c += 1
        if not is_digit(rd(c)):
            return fail(p, ERR_NUMBER, c)
        frac_start = c
        c = scan_digits(c + 1, end)
        frac_end = c
    var b = rd(c)
    if b == 101 or b == 69:
        is_float = True
        c += 1
        var sign = 1
        b = rd(c)
        if b == 43 or b == 45:
            sign = -1 if b == 45 else 1
            c += 1
            b = rd(c)
        if not is_digit(b):
            return fail(p, ERR_NUMBER, c)
        var e = 0
        while is_digit(b):
            if e < 100000:
                e = e * 10 + Int(b - 48)
            c += 1
            b = rd(c)
        exponent = sign * e
    p[].cur = c
    var int_digits = int_end - int_start
    var frac_digits = frac_end - frac_start
    if int_digits + frac_digits > 19:
        while frac_digits > 0 and rd(frac_end - 1) == 48:
            frac_end -= 1
            frac_digits -= 1
        while int_digits > 1 and rd(int_start) == 48:
            int_start += 1
            int_digits -= 1
    var mantissa: UInt64
    if int_digits + frac_digits <= 19:
        mantissa = digits_value(int_start, int_digits)
        if frac_digits > 0:
            mantissa = mantissa * p[].ctx[].pow10[unsafe_offset=frac_digits] + digits_value(frac_start, frac_digits)
        exponent -= frac_digits
    elif not is_float and int_digits == 20:
        mantissa = digits_value(int_start, 19)
        var last = UInt64(rd(int_start + 19) - 48)
        if mantissa > (UInt64(0xFFFFFFFFFFFFFFFF) - last) / 10:
            return float_from_text(p, start, c)
        mantissa = mantissa * 10 + last
    else:
        return float_from_text(p, start, c)
    if not is_float:
        return make_int(negative, mantissa, p, start, c)
    return make_float(negative, mantissa, exponent, p, start, c)


# Digits c[1..18] accumulate into m; returns the count consumed, or 20 when a 20th digit follows.
@always_inline
def digit_chain(c: P8, mut m: UInt64) -> Int:
    comptime for i in range(1, 19):
        var b = c[unsafe_offset=i]
        if not is_digit(b):
            return i
        m = m * 10 + UInt64(b - 48)
    if is_digit(c[unsafe_offset=19]):
        return 20
    return 19


def parse_number[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var start = p[].cur
    var c = start
    var negative = rd(c) == 45
    if negative:
        c += 1
    var mantissa: UInt64 = 0
    var int_digits: Int
    var frac_digits = 0
    var exponent = 0
    var is_float = False
    var b = rd(c)
    if b == 48:
        if is_digit(rd(c + 1)):
            return fail(p, ERR_LEADING_ZERO, start)
        int_digits = 1
        c += 1
    else:
        if not is_digit(b):
            return fail(p, ERR_VALUE, start)
        mantissa = UInt64(b - 48)
        var n = digit_chain(P8(unsafe_from_address=c), mantissa)
        if n == 20:
            return parse_number_slow(p)
        int_digits = n
        c += n
    if rd(c) == 46:
        is_float = True
        c += 1
        b = rd(c)
        if not is_digit(b):
            return fail(p, ERR_NUMBER, c)
        mantissa = mantissa * 10 + UInt64(b - 48)
        var n = digit_chain(P8(unsafe_from_address=c), mantissa)
        if n == 20:
            return parse_number_slow(p)
        frac_digits = n
        c += n
    if int_digits + frac_digits > 19:
        return parse_number_slow(p)
    b = rd(c)
    if b == 101 or b == 69:
        is_float = True
        c += 1
        var sign = 1
        b = rd(c)
        if b == 43 or b == 45:
            sign = -1 if b == 45 else 1
            c += 1
            b = rd(c)
        if not is_digit(b):
            return fail(p, ERR_NUMBER, c)
        var e = 0
        while is_digit(b):
            if e < 100000:
                e = e * 10 + Int(b - 48)
            c += 1
            b = rd(c)
        exponent = sign * e
    p[].cur = c
    exponent -= frac_digits
    if not is_float:
        return make_int(negative, mantissa, p, start, c)
    return make_float(negative, mantissa, exponent, p, start, c)


# ---- containers ----
@no_inline
def grow_stack[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Bool:
    var cap = p[].stack_cap * 2
    var fresh = external_call["malloc", Int](cap * 8)
    if fresh == 0:
        return False
    external_call["memcpy", NoneType](fresh, Int(p[].stack), p[].stack_len * 8)
    if Int(p[].stack) != p[].stack_inline:
        external_call["free", NoneType](Int(p[].stack))
    p[].stack = PI(unsafe_from_address=fresh)
    p[].stack_cap = cap
    return True


@always_inline
def push[o_: Origin[mut=True]](p: Pointer[Parser, o_], value: Int) -> Bool:
    if p[].stack_len == p[].stack_cap:
        if not grow_stack(p):
            decref(value)
            p[].err = ERR_MEMORY
            return False
    p[].stack[unsafe_offset=p[].stack_len] = value
    p[].stack_len += 1
    return True


def parse_array[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var end = p[].end
    p[].depth += 1
    if p[].depth > MAX_DEPTH:
        return fail(p, ERR_DEPTH, p[].cur)
    var c = skip_space(p[].cur + 1, end)
    if c < end and rd(c) == 93:
        p[].cur = c + 1
        p[].depth -= 1
        return external_call["PyList_New", Int](0)
    var base = p[].stack_len
    while True:
        p[].cur = c
        if c >= end:
            return fail(p, ERR_EOF, end)
        var value = parse_at(p, c)
        if value == 0 or not push(p, value):
            return 0
        c = skip_space(p[].cur, end)
        if c >= end:
            return fail(p, ERR_EOF, end)
        var b = rd(c)
        if b == 44:
            var comma = c
            c = skip_space(c + 1, end)
            if c < end and rd(c) == 93:
                return fail(p, ERR_COMMA, comma)
            continue
        if b == 93:
            break
        return fail(p, ERR_SEPARATOR, c)
    var count = p[].stack_len - base
    var lst = external_call["PyList_New", Int](count)
    if lst == 0:
        return 0
    var items = PI(unsafe_from_address=lst + LIST_ITEMS)[]
    external_call["memcpy", NoneType](items, Int(p[].stack) + base * 8, count * 8)
    p[].stack_len = base
    p[].cur = c + 1
    p[].depth -= 1
    return lst


def parse_object[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var end = p[].end
    p[].depth += 1
    if p[].depth > MAX_DEPTH:
        return fail(p, ERR_DEPTH, p[].cur)
    var c = skip_space(p[].cur + 1, end)
    var dict = external_call["PyDict_New", Int]()
    if dict == 0:
        return 0
    if c < end and rd(c) == 125:
        p[].cur = c + 1
        p[].depth -= 1
        return dict
    while True:
        if c >= end:
            _ = fail(p, ERR_EOF, end)
            break
        if rd(c) != 34:
            _ = fail(p, ERR_KEY, c)
            break
        p[].cur = c
        var key = parse_key(p)
        if key == 0:
            break
        c = skip_space(p[].cur, end)
        if c >= end or rd(c) != 58:
            decref(key)
            _ = fail(p, ERR_COLON if c < end else ERR_EOF, c if c < end else end)
            break
        c = skip_space(c + 1, end)
        p[].cur = c
        if c >= end:
            decref(key)
            _ = fail(p, ERR_EOF, end)
            break
        var value = parse_at(p, c)
        if value == 0:
            decref(key)
            break
        var status = external_call["PyDict_SetItem", Int32](dict, key, value)
        decref(key)
        decref(value)
        if status < 0:
            break
        c = skip_space(p[].cur, end)
        if c >= end:
            _ = fail(p, ERR_EOF, end)
            break
        var b = rd(c)
        if b == 44:
            var comma = c
            c = skip_space(c + 1, end)
            if c < end and rd(c) == 125:
                _ = fail(p, ERR_COMMA, comma)
                break
            continue
        if b == 125:
            p[].cur = c + 1
            p[].depth -= 1
            return dict
        _ = fail(p, ERR_SEPARATOR, c)
        break
    decref(dict)
    return 0


@always_inline
def parse_literal[o_: Origin[mut=True]](p: Pointer[Parser, o_], w0: UInt8, w1: UInt8, w2: UInt8, w3: UInt8, length: Int, value: Int) -> Int:
    var c = p[].cur
    var available = p[].end - c
    if available >= length:
        var ok = rd(c + 1) == w1 and rd(c + 2) == w2 and rd(c + 3) == w3
        if length == 5:
            ok = ok and rd(c + 4) == 101
        if ok:
            p[].cur = c + length
            incref(value)
            return value
    # a prefix of the word at the end of the input is "unexpected end of data"
    var i = 1
    while i < available and i < length:
        var expected = w1 if i == 1 else (w2 if i == 2 else (w3 if i == 3 else UInt8(101)))
        if rd(c + i) != expected:
            return fail(p, ERR_VALUE, c)
        i += 1
    if available < length:
        return fail(p, ERR_EOF, p[].end)
    return fail(p, ERR_VALUE, c)


@always_inline
def parse_at[o_: Origin[mut=True]](p: Pointer[Parser, o_], c: Int) -> Int:
    var b = rd(c)
    if b == 34:
        return parse_string(p)
    if b == 123:
        return parse_object(p)
    if b == 91:
        return parse_array(p)
    if b == 116:
        return parse_literal(p, 116, 114, 117, 101, 4, p[].ctx[].py_true)
    if b == 102:
        return parse_literal(p, 102, 97, 108, 115, 5, p[].ctx[].py_false)
    if b == 110:
        return parse_literal(p, 110, 117, 108, 108, 4, p[].ctx[].py_none)
    if b == 45 or is_digit(b):
        return parse_number(p)
    return fail(p, ERR_VALUE, c)


def parse_value[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var c = skip_space(p[].cur, p[].end)
    p[].cur = c
    if c >= p[].end:
        return fail(p, ERR_EOF, p[].end)
    return parse_at(p, c)


# ---- entry points ----
@export
def yjson_mojo_init(py_true: Int, py_false: Int, py_none: Int) abi("C") -> Int:
    var ctx = alloc[Ctx](1)
    ctx[] = Ctx(py_true, py_false, py_none)
    return Int(ctx)


# Parses the NUL-terminated buffer; returns a new reference, or 0 with the error code and
# byte position written to err_out[0] and err_out[1] (code 0 means a Python exception is set).
@export
def yjson_mojo_loads(ctx: Int, data: Int, length: Int, err_out: Int) abi("C") -> Int:
    var inline_stack = unsafe_stack_allocation[INLINE_STACK, Int]()
    var parser = Parser(ctx, data, length, Int(inline_stack))
    var p = Pointer(to=parser)
    var result = 0
    if length == 0:
        _ = fail(p, ERR_EMPTY, data)
    elif length >= 3 and rd(data) == 0xEF and rd(data + 1) == 0xBB and rd(data + 2) == 0xBF:
        _ = fail(p, ERR_BOM, data)
    else:
        result = parse_value(p)
        if result != 0:
            var c = skip_space(p[].cur, p[].end)
            if c < p[].end:
                decref(result)
                result = 0
                _ = fail(p, ERR_TRAILING, c)
    for i in range(p[].stack_len):
        decref(p[].stack[unsafe_offset=i])
    if Int(p[].stack) != p[].stack_inline:
        external_call["free", NoneType](Int(p[].stack))
    if p[].scratch != 0:
        external_call["free", NoneType](p[].scratch)
    var out = PI(unsafe_from_address=err_out)
    out[] = p[].err
    out[unsafe_offset=1] = p[].err_at
    return result

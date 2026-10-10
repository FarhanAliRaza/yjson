# Strict JSON decoder: untyped Python values and direct typed dataclass construction.
# CPython layouts and schema offsets are probed for the target interpreter.
from std.ffi import external_call
from std.memory import alloc, unsafe_stack_allocation
from std.bit import count_trailing_zeros, count_leading_zeros
from std.simd import pack_bits
from std.sys.intrinsics import likely, unlikely
from std.sys import get_defined_int
from std.memory import bitcast
from std.collections import Array

comptime P32 = Pointer[Int32, MutUntrackedOrigin]
comptime PLAN_SIZE = get_defined_int["YJSON_PLAN_SIZE"]()
comptime PLAN_KIND = get_defined_int["YJSON_PLAN_KIND"]()
comptime PLAN_ITEM = get_defined_int["YJSON_PLAN_ITEM"]()
comptime PLAN_FIELDS = get_defined_int["YJSON_PLAN_FIELDS"]()
comptime PLAN_NFIELDS = get_defined_int["YJSON_PLAN_NFIELDS"]()
comptime PLAN_SEEN_WORDS = get_defined_int["YJSON_PLAN_SEEN_WORDS"]()
comptime PLAN_TABLE = get_defined_int["YJSON_PLAN_TABLE"]()
comptime PLAN_MASK = get_defined_int["YJSON_PLAN_MASK"]()
comptime PLAN_KEY_INDEX = get_defined_int["YJSON_PLAN_KEY_INDEX"]()
comptime PLAN_POST_INIT = get_defined_int["YJSON_PLAN_POST_INIT"]()
comptime FIELD_SIZE = get_defined_int["YJSON_FIELD_SIZE"]()
comptime FIELD_NAME = get_defined_int["YJSON_FIELD_NAME"]()
comptime FIELD_PLAN = get_defined_int["YJSON_FIELD_PLAN"]()
comptime FIELD_DEFAULT_VALUE = get_defined_int["YJSON_FIELD_DEFAULT_VALUE"]()
comptime FIELD_DEFAULT_KIND = get_defined_int["YJSON_FIELD_DEFAULT_KIND"]()
comptime FIELD_SLOT = get_defined_int["YJSON_FIELD_SLOT"]()
comptime FIELD_HASH = get_defined_int["YJSON_FIELD_HASH"]()
comptime FIELD_LENGTH = get_defined_int["YJSON_FIELD_LENGTH"]()
comptime FIELD_BYTES = get_defined_int["YJSON_FIELD_BYTES"]()

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
comptime STR_COMPACT_DATA = get_defined_int["YJSON_STR_COMPACT_DATA", 72]()

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
comptime ERR_LOW_SURROGATE = 19
comptime ERR_LONE_SURROGATE = 20
comptime ERR_TYPE = 21
comptime ERR_MISSING = 22
comptime ERR_OBJECT_SEPARATOR = 23
comptime ERR_SIGN = 24



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
    var err_plan: Int
    var err_detail: Int
    var err_at: Int
    var stack: PI
    var stack_len: Int
    var stack_cap: Int
    var stack_inline: Int
    var scratch: Int
    var scratch_cap: Int
    var wide: Int        # UCS-2 scratch for non-ASCII strings, in bytes
    var wide_cap: Int
    var ctx: Pointer[Ctx, MutUntrackedOrigin]

    def __init__(out self, ctx: Int, data: Int, length: Int, inline_stack: Int):
        self.start = data
        self.cur = data
        self.end = data + length
        self.depth = 0
        self.err = 0
        self.err_plan = 0
        self.err_detail = 0
        self.err_at = 0
        self.stack = PI(unsafe_from_address=inline_stack)
        self.stack_len = 0
        self.stack_cap = INLINE_STACK
        self.stack_inline = inline_stack
        self.scratch = 0
        self.scratch_cap = 0
        self.wide = 0
        self.wide_cap = 0
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
    # JSON whitespace occupies bits 9, 10, 13 and 32 of the mask.
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
    # Keep the ordinary-space path and non-whitespace exits separate: combining
    # them into one Boolean made LLVM emit shifts, flag chains and cmov here.
    comptime for i in range(2):
        if c >= end:
            return c
        var b = rd(c)
        if unlikely(b != 32):
            if b > 13:
                return c
            # Clearing bit 2 maps CR (13) to TAB (9).
            if b != 10 and (b & UInt8(0xFB)) != 9:
                return c
        c += 1
    while end - c >= 16:
        var v = P8(unsafe_from_address=c).unsafe_load[width=16]()
        var white = v.eq(32) | v.eq(10) | v.eq(13) | v.eq(9)
        var mask = ~UInt16(pack_bits(white))
        if mask != 0:
            return c + Int(count_trailing_zeros(mask))
        c += 16
    while c < end:
        var b = rd(c)
        if unlikely(b != 32):
            if b > 13:
                break
            if b != 10 and (b & UInt8(0xFB)) != 9:
                break
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
        # An invalid byte already present takes precedence over a missing byte.
        # Preserve the public diagnostic for malformed partial Unicode escapes.
        var result = 0
        for i in range(2, 6):
            if c + i >= end:
                return -2
            var digit = Int(hex[unsafe_offset=Int(rd(c + i))])
            if digit >= 16:
                return -1
            result = (result << 4) | digit
        return result
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
    var s = utf8_string(p, src, n)
    if s == 0:
        return fail(p, ERR_UTF8, quote)
    return s


# Builds a str from UTF-8 the way CPython's decoder would (RFC 3629: no
# overlongs, no surrogates, nothing past U+10FFFF) in one pass that decodes
# into a UCS-2 scratch, widening ASCII words with one vector instruction.
# The result is then narrowed or copied into a str of the right kind; the
# rare 4-byte sequence takes a second UCS-4 pass. Returns 0 on invalid input,
# with no error set.
@no_inline
def utf8_string[o_: Origin[mut=True]](p: Pointer[Parser, o_], src: Int, n: Int) -> Int:
    if p[].wide_cap < 2 * n + 16:
        var fresh = external_call["realloc", Int](p[].wide, 2 * n + 16)
        if fresh == 0:
            return 0
        p[].wide = fresh
        p[].wide_cap = 2 * n + 16
    var out = Pointer[UInt16, MutUntrackedOrigin](unsafe_from_address=p[].wide)
    var i = 0
    var j = 0
    var widest = UInt16(0)   # OR of every decoded non-ASCII character
    var four = False
    while i < n:
        var b = rd(src + i)
        if b < 0x80:
            if n - i >= 8:
                var w = P8(unsafe_from_address=src + i).unsafe_load[width=8]()
                if (w & 0x80).reduce_or() == 0:
                    out.unsafe_offset(j).unsafe_store(w.cast[DType.uint16]())
                    i += 8
                    j += 8
                    continue
            out[unsafe_offset=j] = UInt16(b)
            i += 1
            j += 1
            continue
        if b < 0xE0:
            if b < 0xC2 or i + 1 >= n:
                return 0
            var b1 = rd(src + i + 1)
            if (b1 & 0xC0) != 0x80:
                return 0
            var cp = (UInt16(b & 0x1F) << 6) | UInt16(b1 & 0x3F)
            out[unsafe_offset=j] = cp
            widest |= cp
            i += 2
            j += 1
            continue
        if b < 0xF0:
            if i + 2 >= n:
                return 0
            var b1 = rd(src + i + 1)
            var b2 = rd(src + i + 2)
            if (b1 & 0xC0) != 0x80 or (b2 & 0xC0) != 0x80:
                return 0
            if b == 0xE0 and b1 < 0xA0:
                return 0
            if b == 0xED and b1 >= 0xA0:
                return 0
            var cp = (UInt16(b & 0x0F) << 12) | (UInt16(b1 & 0x3F) << 6) | UInt16(b2 & 0x3F)
            out[unsafe_offset=j] = cp
            widest |= cp
            i += 3
            j += 1
            # Text in a 3-byte script (CJK, Devanagari, ...) continues with
            # more of the same: decode such runs without the full dispatch.
            # Leads E1..EC and EE..EF have no special second-byte rule.
            while i + 3 <= n:
                var lb = rd(src + i)
                var l1 = rd(src + i + 1)
                var l2 = rd(src + i + 2)
                if (lb - 0xE1) >= 0x0C and (lb - 0xEE) >= 0x02:
                    break
                if ((l1 & 0xC0) | ((l2 & 0xC0) << 2)) != 0x280:
                    break
                cp = (UInt16(lb & 0x0F) << 12) | (UInt16(l1 & 0x3F) << 6) | UInt16(l2 & 0x3F)
                out[unsafe_offset=j] = cp
                i += 3
                j += 1
            widest |= 0x800
            continue
        if b >= 0xF5 or i + 3 >= n:
            return 0
        var c1 = rd(src + i + 1)
        if (c1 & 0xC0) != 0x80 or (rd(src + i + 2) & 0xC0) != 0x80 or (rd(src + i + 3) & 0xC0) != 0x80:
            return 0
        if b == 0xF0 and c1 < 0x90:
            return 0
        if b == 0xF4 and c1 >= 0x90:
            return 0
        four = True
        out[unsafe_offset=j] = 0xFFFF
        i += 4
        j += 1
    if four:
        return utf8_string_ucs4(src, n, j)
    if widest < 0x80:
        return ascii_string(src, n)
    if widest < 0x100:
        var s = external_call["PyUnicode_New", Int](j, 255)
        if s == 0:
            return 0
        var dst = P8(unsafe_from_address=s + STR_COMPACT_DATA)
        var k = 0
        while k + 8 <= j:
            dst.unsafe_offset(k).unsafe_store(out.unsafe_offset(k).unsafe_load[width=8]().cast[DType.uint8]())
            k += 8
        while k < j:
            dst[unsafe_offset=k] = UInt8(out[unsafe_offset=k])
            k += 1
        return s
    var s = external_call["PyUnicode_New", Int](j, 65535)
    if s == 0:
        return 0
    external_call["memcpy", NoneType](s + STR_COMPACT_DATA, Int(out), 2 * j)
    return s


# Strings with a character past U+FFFF, already validated: a UCS-4 fill.
@no_inline
def utf8_string_ucs4(src: Int, n: Int, chars: Int) -> Int:
    var s = external_call["PyUnicode_New", Int](chars, 0x10FFFF)
    if s == 0:
        return 0
    var out = Pointer[UInt32, MutUntrackedOrigin](unsafe_from_address=s + STR_COMPACT_DATA)
    var j = 0
    var i = 0
    while i < n:
        var b = rd(src + i)
        if b < 0x80:
            out[unsafe_offset=j] = UInt32(b)
            i += 1
        elif b < 0xE0:
            out[unsafe_offset=j] = (UInt32(b & 0x1F) << 6) | UInt32(rd(src + i + 1) & 0x3F)
            i += 2
        elif b < 0xF0:
            out[unsafe_offset=j] = (UInt32(b & 0x0F) << 12) | (UInt32(rd(src + i + 1) & 0x3F) << 6) | UInt32(rd(src + i + 2) & 0x3F)
            i += 3
        else:
            out[unsafe_offset=j] = (UInt32(b & 0x07) << 18) | (UInt32(rd(src + i + 1) & 0x3F) << 12) | (UInt32(rd(src + i + 2) & 0x3F) << 6) | UInt32(rd(src + i + 3) & 0x3F)
            i += 4
        j += 1
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
                        var prefix = c + 6 >= end or (rd(c + 6) == 92 and c + 7 >= end)
                        return fail(p, ERR_EOF if prefix else ERR_SURROGATE, end if prefix else c)
                    if rd(c + 6) != 92 or rd(c + 7) != 117:
                        return fail(p, ERR_SURROGATE, c)
                    var low = hex4(hex, c + 6, end)
                    if low == -2:
                        return fail(p, ERR_EOF, end)
                    if low < 0xDC00 or low > 0xDFFF:
                        return fail(p, ERR_LOW_SURROGATE, c)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00)
                    c += 12
                elif cp >= 0xDC00 and cp <= 0xDFFF:
                    return fail(p, ERR_LONE_SURROGATE, c)
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
    if n >= 8 and n <= 16:
        # Both words lie inside the validated key; overlap covers lengths 8–15.
        var data = cached + STR_ASCII_DATA
        if P64(unsafe_from_address=data)[] != P64(unsafe_from_address=start)[]:
            return False
        return P64(unsafe_from_address=data + n - 8)[] == P64(unsafe_from_address=start + n - 8)[]
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


@no_inline
def eisel_subnormal(m0: UInt64, power2: Int) -> UInt64:
    # Keep subnormal rounding out of the normal-float instruction path.
    if -power2 + 1 >= 64:
        return 0
    var m = m0 >> UInt64(-power2 + 1)
    m += m & 1
    m >>= 1
    var exponent = 0 if m < (UInt64(1) << 52) else 1
    return m | (UInt64(exponent) << 52)


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
    if unlikely(power2 <= 0):
        out = eisel_subnormal(m, power2)
        return True
    if unlikely(low <= 1):
        if exponent >= -4 and exponent <= 23 and (m & 3) == 1 and (m << UInt64(shift)) == high:
            m &= ~UInt64(1)
    m += m & 1
    m >>= 1
    if unlikely(m >= (UInt64(2) << 52)):
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
        # Up to 30 bits: the signed constructor's single-digit fast path. Wider:
        # the unsigned constructor builds the digits with less work.
        if mantissa < (UInt64(1) << 30):
            return external_call["PyLong_FromLongLong", Int](Int64(mantissa))
        return external_call["PyLong_FromUnsignedLongLong", Int](mantissa)
    if mantissa < (UInt64(1) << 63):
        return external_call["PyLong_FromLongLong", Int](-Int64(mantissa))
    if mantissa == (UInt64(1) << 63):
        return external_call["PyLong_FromLongLong", Int](Int64(-9223372036854775807) - 1)
    return float_from_text(p, start, c)


@no_inline
def make_float_general[o_: Origin[mut=True]](negative: Bool, mantissa: UInt64, exponent: Int, p: Pointer[Parser, o_], start: Int, c: Int) -> Int:
    # Keep Eisel-Lemire's checks and register demand out of integer/Clinger paths.
    ref ctx = p[].ctx[]
    var bits: UInt64 = 0
    if eisel_lemire(ctx.powers, mantissa, exponent, bits):
        var value = bits_to_double(bits)
        return external_call["PyFloat_FromDouble", Int](-value if negative else value)
    return float_from_text(p, start, c)


@always_inline
def make_float[o_: Origin[mut=True]](negative: Bool, mantissa: UInt64, exponent: Int, p: Pointer[Parser, o_], start: Int, c: Int) -> Int:
    if mantissa <= (UInt64(1) << 53) and exponent >= -22 and exponent <= 22:
        var value = Float64(Int64(mantissa))
        var exact10 = p[].ctx[].exact10
        if exponent < 0:
            value = value / exact10[unsafe_offset=-exponent]
        else:
            value = value * exact10[unsafe_offset=exponent]
        return external_call["PyFloat_FromDouble", Int](-value if negative else value)
    return make_float_general(negative, mantissa, exponent, p, start, c)


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
            return fail(p, ERR_NUMBER if c < end else ERR_EOF, c)
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
            return fail(p, ERR_NUMBER if c < end else ERR_EOF, c)
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


@always_inline
def parse_number_at[array_number: Bool, o_: Origin[mut=True]](p: Pointer[Parser, o_], start: Int) -> Tuple[Int, Int]:
    # Specialize at compile time: bulk fractions and a local cursor for arrays,
    # the compact original path for scalar and object-field numbers.
    var c = start
    var negative = rd(c) == 45
    if negative:
        c += 1
    var mantissa: UInt64 = 0
    var int_digits: Int
    var frac_digits = 0
    var exponent = 0
    var b = rd(c)
    if b == 48:
        if is_digit(rd(c + 1)):
            return (fail(p, ERR_LEADING_ZERO, start), start)
        int_digits = 1
        c += 1
    else:
        if not is_digit(b):
            var at_end = c >= p[].end
            return (fail(p, ERR_SIGN if negative else (ERR_EOF if at_end else ERR_VALUE), p[].end if at_end else start), start)
        mantissa = UInt64(b - 48)
        var n = digit_chain(P8(unsafe_from_address=c), mantissa)
        if n == 20:
            p[].cur = start
            var value = parse_number_slow(p)
            return (value, p[].cur)
        int_digits = n
        c += n
    b = rd(c)
    if b != 46 and (b | UInt8(0x20)) != 101:
        if not array_number:
            p[].cur = c
        return (make_int(negative, mantissa, p, start, c), c)
    if b == 46:
        c += 1
        b = rd(c)
        if not is_digit(b):
            return (fail(p, ERR_NUMBER if c < p[].end else ERR_EOF, c), start)
        mantissa = mantissa * 10 + UInt64(b - 48)
        # The word is read only when all eight bytes are inside the input.
        if array_number and p[].end - c >= 9:
            var word = P64(unsafe_from_address=c + 1)[]
            if is_eight_digits(word):
                mantissa = mantissa * 100000000 + eight_digits_value(word)
                c += 8
                frac_digits = 8
        var n = digit_chain(P8(unsafe_from_address=c), mantissa)
        if n == 20:
            p[].cur = start
            var value = parse_number_slow(p)
            return (value, p[].cur)
        frac_digits += n
        c += n
    if int_digits + frac_digits > 19:
        p[].cur = start
        var value = parse_number_slow(p)
        return (value, p[].cur)
    b = rd(c)
    if b == 101 or b == 69:
        c += 1
        var sign = 1
        b = rd(c)
        if b == 43 or b == 45:
            sign = -1 if b == 45 else 1
            c += 1
            b = rd(c)
        if not is_digit(b):
            return (fail(p, ERR_NUMBER if c < p[].end else ERR_EOF, c), start)
        var e = 0
        while is_digit(b):
            if e < 100000:
                e = e * 10 + Int(b - 48)
            c += 1
            b = rd(c)
        exponent = sign * e
    exponent -= frac_digits
    if not array_number:
        p[].cur = c
    return (make_float(negative, mantissa, exponent, p, start, c), c)


def parse_number[o_: Origin[mut=True]](p: Pointer[Parser, o_]) -> Int:
    var result, _ = parse_number_at[False](p, p[].cur)
    return result


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


def parse_array[typed: Bool, o_: Origin[mut=True]](p: Pointer[Parser, o_], item: Int) -> Int:
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
        if c >= end:
            return fail(p, ERR_EOF, end)
        var value: Int
        var stop: Int
        var first = rd(c)
        if typed:
            p[].cur = c
            value = parse_typed(p, item)
            stop = p[].cur
        elif first == 45 or is_digit(first):
            # Keep the numeric cursor in registers instead of in Parser.cur.
            value, stop = parse_number_at[True](p, c)
        else:
            p[].cur = c
            value = parse_at(p, c)
            stop = p[].cur
        if value == 0 or not push(p, value):
            return 0
        c = skip_space(stop, end)
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
    if count <= 4:
        # Avoid a libc call for the small lists common in nested JSON arrays.
        var target = P64(unsafe_from_address=items)
        var source = P64(unsafe_from_address=Int(p[].stack) + base * 8)
        if count >= 2:
            var first = source.unsafe_load[width=2]()
            var last_source = P64(unsafe_from_address=Int(source) + (count - 2) * 8)
            var last = last_source.unsafe_load[width=2]()
            target.unsafe_store[width=2](first)
            P64(unsafe_from_address=items + (count - 2) * 8).unsafe_store[width=2](last)
        elif count == 1:
            target[] = source[]
    else:
        external_call["memcpy", NoneType](items, Int(p[].stack) + base * 8, count * 8)
    p[].stack_len = base
    p[].cur = c + 1
    p[].depth -= 1
    return lst


def parse_object[typed: Bool, o_: Origin[mut=True]](p: Pointer[Parser, o_], item: Int) -> Int:
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
        var value: Int
        if typed:
            value = parse_typed(p, item)
        else:
            value = parse_at(p, c)
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
        _ = fail(p, ERR_OBJECT_SEPARATOR, c)
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
        return parse_object[False](p, 0)
    if b == 91:
        return parse_array[False](p, 0)
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


# ---- typed traversal; annotation plan construction and Python hooks use the C API ----
@always_inline
def word(address: Int, offset: Int) -> Int:
    return PI(unsafe_from_address=address + offset)[]


@always_inline
def small_word(address: Int, offset: Int) -> Int:
    return Int(P32(unsafe_from_address=address + offset)[])


@always_inline
def type_error[o_: Origin[mut=True]](p: Pointer[Parser, o_], plan: Int, at: Int, is_float: Int = 0) -> Int:
    p[].err_plan = plan
    p[].err_detail = is_float
    return fail(p, ERR_TYPE, at)


@always_inline
def field_matches(field: Int, start: Int, length: Int) -> Bool:
    var bytes = word(field, FIELD_BYTES)
    return bytes != 0 and word(field, FIELD_LENGTH) == length and external_call["memcmp", Int32](bytes, start, length) == 0


def parse_dataclass[o_: Origin[mut=True]](p: Pointer[Parser, o_], plan: Int) -> Int:
    var end = p[].end
    p[].depth += 1
    if p[].depth > MAX_DEPTH:
        return fail(p, ERR_DEPTH, p[].cur)
    var c = skip_space(p[].cur + 1, end)
    var obj = external_call["yjson_decoder_alloc", Int](plan)
    if obj == 0:
        return 0
    var seen_inline = unsafe_stack_allocation[8, UInt64]()
    var seen = P64(unsafe_from_address=Int(seen_inline))
    var words = small_word(plan, PLAN_SEEN_WORDS)
    if words > 8:
        seen = P64(unsafe_from_address=external_call["malloc", Int](words * 8))
        if Int(seen) == 0:
            decref(obj)
            return fail(p, ERR_MEMORY, c)
    external_call["memset", NoneType](Int(seen), 0, words * 8)
    var fields = word(plan, PLAN_FIELDS)
    var count = small_word(plan, PLAN_NFIELDS)
    var table = P32(unsafe_from_address=word(plan, PLAN_TABLE))
    var mask = small_word(plan, PLAN_MASK)
    var expected = 0
    var complete = c < end and rd(c) == 125
    if complete:
        c += 1
    while not complete:
        if c >= end:
            _ = fail(p, ERR_EOF, end)
            break
        if rd(c) != 34:
            _ = fail(p, ERR_KEY, c)
            break
        var start = c + 1
        var special = scan[True](start, end)
        var index = -1
        if special < end and rd(special) == 34:
            var length = special - start
            var next = fields + expected * FIELD_SIZE
            if count != 0 and field_matches(next, start, length):
                index = expected
            else:
                var hash = key_hash(start, length)
                var at = Int(hash & UInt64(mask))
                while table[unsafe_offset=at] >= 0:
                    var candidate = Int(table[unsafe_offset=at])
                    var field = fields + candidate * FIELD_SIZE
                    if UInt64(word(field, FIELD_HASH)) == hash and field_matches(field, start, length):
                        index = candidate
                        break
                    at = (at + 1) & mask
            p[].cur = special + 1
        else:
            var key = string_from(p, start, special)
            if key == 0:
                break
            var found = external_call["PyDict_GetItemWithError", Int](word(plan, PLAN_KEY_INDEX), key)
            decref(key)
            if found != 0:
                index = external_call["PyLong_AsLong", Int](found)
            elif external_call["PyErr_Occurred", Int]() != 0:
                break
        c = skip_space(p[].cur, end)
        if c >= end:
            _ = fail(p, ERR_EOF, end)
            break
        if rd(c) != 58:
            _ = fail(p, ERR_COLON, c)
            break
        p[].cur = c + 1
        if index >= 0:
            var field = fields + index * FIELD_SIZE
            var value = parse_typed(p, word(field, FIELD_PLAN))
            if value == 0:
                break
            var status = external_call["yjson_decoder_set_field", Int32](obj, field, value)
            decref(value)
            if status < 0:
                break
            seen[unsafe_offset=index // 64] |= UInt64(1) << UInt64(index % 64)
            expected = index + 1 if index + 1 < count else 0
        else:
            var value = parse_value(p)
            if value == 0:
                break
            decref(value)
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
            c += 1
            complete = True
            break
        _ = fail(p, ERR_OBJECT_SEPARATOR, c)
        break
    if complete:
        for i in range(count):
            if (seen[unsafe_offset=i // 64] & (UInt64(1) << UInt64(i % 64))) != 0:
                continue
            var field = fields + i * FIELD_SIZE
            var kind = small_word(field, FIELD_DEFAULT_KIND)
            var value: Int
            if kind == 1:
                value = word(field, FIELD_DEFAULT_VALUE)
                incref(value)
            elif kind == 2:
                value = external_call["PyObject_CallNoArgs", Int](word(field, FIELD_DEFAULT_VALUE))
            elif kind == 3:
                continue
            else:
                p[].err_plan = plan
                p[].err_detail = field
                _ = fail(p, ERR_MISSING, c - 1)
                complete = False
                break
            if value == 0:
                complete = False
                break
            var status = external_call["yjson_decoder_set_field", Int32](obj, field, value)
            decref(value)
            if status < 0:
                complete = False
                break
        if complete and small_word(plan, PLAN_POST_INIT) != 0:
            var result = external_call["yjson_decoder_post_init", Int](obj)
            if result == 0:
                complete = False
            else:
                decref(result)
    if words > 8:
        external_call["free", NoneType](Int(seen))
    if not complete:
        decref(obj)
        return 0
    p[].cur = c
    p[].depth -= 1
    return obj


def parse_typed[o_: Origin[mut=True]](p: Pointer[Parser, o_], plan: Int) -> Int:
    var kind = small_word(plan, PLAN_KIND)
    if kind == 0:
        return parse_value(p)
    var c = skip_space(p[].cur, p[].end)
    p[].cur = c
    if c >= p[].end:
        return fail(p, ERR_EOF, p[].end)
    var b = rd(c)
    if kind == 1 or kind == 2:
        if b != 45 and not is_digit(b):
            return type_error(p, plan, c)
        var value = parse_number(p)
        if value == 0:
            return 0
        if external_call["yjson_decoder_is_int", Int32](value) != 0:
            if kind == 1:
                return value
            var as_double = external_call["PyLong_AsDouble", Float64](value)
            decref(value)
            if as_double == -1.0 and external_call["PyErr_Occurred", Int]() != 0:
                return 0
            return external_call["PyFloat_FromDouble", Int](as_double)
        if kind == 2:
            return value
        decref(value)
        return type_error(p, plan, c, 1)
    if kind == 3:
        if b != 34:
            return type_error(p, plan, c)
        return parse_string(p)
    if kind == 4:
        if b == 116:
            return parse_literal(p, 116, 114, 117, 101, 4, p[].ctx[].py_true)
        if b == 102:
            return parse_literal(p, 102, 97, 108, 115, 5, p[].ctx[].py_false)
        return type_error(p, plan, c)
    if kind == 5 or kind == 6:
        if b == 110:
            return parse_literal(p, 110, 117, 108, 108, 4, p[].ctx[].py_none)
        if kind == 5:
            return type_error(p, plan, c)
        return parse_typed(p, word(plan, PLAN_ITEM))
    if kind == 7:
        if b != 91:
            return type_error(p, plan, c)
        return parse_array[True](p, word(plan, PLAN_ITEM))
    if kind == 8:
        if b != 123:
            return type_error(p, plan, c)
        return parse_object[True](p, word(plan, PLAN_ITEM))
    if b != 123:
        return type_error(p, plan, c)
    return parse_dataclass(p, plan)


# ---- entry points ----
@export
def yjson_mojo_init(py_true: Int, py_false: Int, py_none: Int) abi("C") -> Int:
    var ctx = alloc[Ctx](1)
    ctx[] = Ctx(py_true, py_false, py_none)
    return Int(ctx)


@export
def yjson_mojo_destroy(address: Int) abi("C"):
    var ctx = Pointer[Ctx, MutUntrackedOrigin](unsafe_from_address=address)
    for i in range(SLOTS):
        var key = ctx[].key_cache[unsafe_offset=i]
        if key != 0:
            decref(key)
    ctx[].key_cache.unsafe_free()
    ctx[].next_slot.unsafe_free()
    ctx[].exact10.unsafe_free()
    ctx[].pow10.unsafe_free()
    ctx[].hex.unsafe_free()
    ctx[].escapes.unsafe_free()
    ctx.unsafe_free()


# Parses the NUL-terminated buffer; returns a new reference, or 0 with the error code and
# byte position written to err_out[0] and err_out[1] (code 0 means a Python exception is set).
def loads_impl[typed: Bool](ctx: Int, data: Int, length: Int, plan: Int, err_out: Int) -> Int:
    var inline_stack = unsafe_stack_allocation[INLINE_STACK, Int]()
    var parser = Parser(ctx, data, length, Int(inline_stack))
    var p = Pointer(to=parser)
    var result = 0
    if length == 0:
        _ = fail(p, ERR_EMPTY, data)
    elif length >= 3 and rd(data) == 0xEF and rd(data + 1) == 0xBB and rd(data + 2) == 0xBF:
        _ = fail(p, ERR_BOM, data)
    else:
        if typed and plan != 0:
            result = parse_typed(p, plan)
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
    if p[].wide != 0:
        external_call["free", NoneType](p[].wide)
    var out = PI(unsafe_from_address=err_out)
    out[] = p[].err
    out[unsafe_offset=1] = p[].err_at
    if typed:
        out[unsafe_offset=2] = p[].err_plan
        out[unsafe_offset=3] = p[].err_detail
    return result


@export
def yjson_mojo_loads(ctx: Int, data: Int, length: Int, err_out: Int) abi("C") -> Int:
    return loads_impl[False](ctx, data, length, 0, err_out)


@export
def yjson_mojo_loads_typed(ctx: Int, data: Int, length: Int, plan: Int, err_out: Int) abi("C") -> Int:
    return loads_impl[True](ctx, data, length, plan, err_out)

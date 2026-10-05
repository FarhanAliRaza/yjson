import struct, random, math
KMIN, KMAX = -292, 326
def floor_log2_pow10(e): return (e * 1741647) >> 19
def floor_log10_pow2(e): return (e * 1262611) >> 22
def floor_log10_34_pow2(e): return (e * 1262611 - 524031) >> 22
def g_of(k):
    e = floor_log2_pow10(k) - 127
    # g = floor(10^k * 2^-e) + 1, exactly, using rationals
    if k >= 0:
        num, den = 10**k, 1
    else:
        num, den = 1, 10**(-k)
    if e >= 0: den <<= e
    else: num <<= -e
    g = num // den + 1
    assert 2**127 <= g < 2**128, (k, g.bit_length())
    return g
TABLE = {k: g_of(k) for k in range(KMIN, KMAX + 1)}
M64 = 2**64 - 1
def round_to_odd(g, cp):
    hi, lo = g >> 64, g & M64
    x = lo * cp; y = hi * cp
    y0 = (y & M64) + (x >> 64)
    y1 = (y >> 64) + (y0 >> 64); y0 &= M64
    return y1 | (1 if y0 > 1 else 0)
def to_decimal(f):
    bits = struct.unpack("<Q", struct.pack("<d", f))[0]
    m = bits & ((1 << 52) - 1); ex = (bits >> 52) & 0x7FF
    if ex != 0:
        c = m | (1 << 52); q = ex - 1075
        if 0 <= -q < 53 and (c & ((1 << -q) - 1)) == 0:
            d, k = c >> -q, 0
            while d % 10 == 0: d //= 10; k += 1
            return d, k
    else:
        c = m; q = 1 - 1075
    even = c % 2 == 0
    closer = (m == 0 and ex > 1)
    cbl, cb, cbr = 4*c - 2 + closer, 4*c, 4*c + 2
    k = floor_log10_34_pow2(q) if closer else floor_log10_pow2(q)
    h = q + floor_log2_pow10(-k) + 1
    g = TABLE[-k]
    vbl, vb, vbr = round_to_odd(g, cbl << h), round_to_odd(g, cb << h), round_to_odd(g, cbr << h)
    lower = vbl + (0 if even else 1); upper = vbr - (0 if even else 1)
    s = vb // 4
    res = None
    if s >= 10:
        sp = s // 10
        up = lower <= 40*sp; wp = 40*sp + 40 <= upper
        if up != wp: res = (sp + wp, k + 1)
    if res is None:
        u = lower <= 4*s; w = 4*s + 4 <= upper
        if u != w: res = (s + w, k)
        else:
            mid = 4*s + 2
            res = (s + (vb > mid or (vb == mid and (s & 1))), k)
    d, k = res
    while d % 10 == 0: d //= 10; k += 1
    return d, k
def py_digits(f):
    r = repr(f)
    mant, _, e = r.partition("e")
    e = int(e) if e else 0
    ip, _, fp = mant.partition(".")
    fp = fp.rstrip("0") if fp else ""
    digits = (ip + fp).lstrip("0") or "0"
    exp = e - len(fp)
    d = int(digits);
    while d and d % 10 == 0: d //= 10; exp += 1
    return d, exp
if __name__ == "__main__":
    random.seed(1)
    tests = [5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, 1e23, 9007199254740993.0, 0.1, 0.3, 1e16, 1e22, 123.456, 2.0**-1074 * 3]
    tests += [2.0**e for e in range(-1074, 1024)]
    tests += [struct.unpack("<d", struct.pack("<Q", random.getrandbits(63)))[0] for _ in range(300000)]
    tests += [random.uniform(0, 1e6) for _ in range(100000)] + [float(i) * 1.1 for i in range(50000)]
    bad = 0
    for f in tests:
        if not math.isfinite(f) or f == 0: continue
        f = abs(f)
        if to_decimal(f) != py_digits(f):
            bad += 1
            if bad < 5: print("MISMATCH", repr(f), to_decimal(f), py_digits(f))
    print("tested", len(tests), "mismatches", bad)

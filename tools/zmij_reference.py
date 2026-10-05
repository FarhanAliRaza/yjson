import struct, random, math, sys
sys.path.insert(0, __import__("os").path.dirname(__file__))
from ref import py_digits
T = {}
for line in open(__import__("os").path.join(__import__("os").path.dirname(__file__), "zmij_pow10_table.txt")):
    e, hi, lo = map(int, line.split()); T[e] = (hi, lo)
M = 2**64 - 1
def umul192_hi128(xh, xl, y):
    p = xh * y
    lo = (p & M) + ((xl * y) >> 64)
    return ((p >> 64) + (lo >> 64)) & M, lo & M
def hi64_add(x, y, c): return ((x * y + c) >> 64) & M
def ces(bin_exp, dec_exp): return bin_exp + ((-dec_exp * 217707) >> 16) + 1
def core(sig, raw_exp, regular):
    bin_exp = raw_exp - 1075
    if not regular:
        dec_exp = (bin_exp * 315653 - 131072) >> 20
        shift = ces(bin_exp, dec_exp + 1) + 9
        hi, lo = T[-dec_exp - 1]
        ph, pl = umul192_hi128(hi, lo, (sig << shift) & M)
        integral = ph >> 9; frac = ((ph << 55) | (pl >> 9)) & M
        half = hi >> (10 - shift)
        ru = half > (M - frac); rd = (half >> 1) > frac
        integral += ru
        digit = hi64_add(frac, 10, (1 << 63) - 1)
        lo2 = hi64_add((frac - (half >> 1)) & M, 10, M)
        if digit < lo2: digit = lo2
        return integral, dec_exp, digit, (ru + rd) == 0
    dec_exp = (bin_exp * 315653) >> 20
    shift = ces(bin_exp, dec_exp + 1) + 9
    even = 1 - (sig & 1)
    hi, lo = T[-dec_exp - 1]
    ph, pl = umul192_hi128(hi, lo, (sig << shift) & M)
    integral = ph >> 9; frac = ((ph << 55) | (pl >> 9)) & M
    half = (hi >> (10 - shift)) + even
    ru = frac + half > M; rd = half > frac
    integral += ru
    digit = hi64_add(frac, 10, (1 << 63) + 6)
    if frac == 1 << 62: digit = 2
    return integral, dec_exp, digit, (ru + rd) == 0
def to_dec(f):
    bits = struct.unpack("<Q", struct.pack("<d", f))[0]
    raw = (bits >> 52) & 0x7FF; m = bits & ((1 << 52) - 1)
    if raw == 0: sig, raw, reg = m, 1, True
    else: sig, reg = m | (1 << 52), m != 0
    integral, k, digit, has = core(sig, raw, reg)
    d = integral * 10 + (digit if has else 0)
    while d % 10 == 0: d //= 10; k += 1
    return d, k
if __name__ == "__main__":
    random.seed(3); bad = 0
    tests = [5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, 1e23, 0.1, 0.3, 1.0, 3.0, 1e16, 1e22, 123.456]
    tests += [2.0**e for e in range(-1074, 1024)] + [3 * 2.0**e for e in range(-1074, 1022)]
    tests += [struct.unpack("<d", struct.pack("<Q", random.getrandbits(63)))[0] for _ in range(400000)]
    tests += [random.uniform(0, 1e6) for _ in range(100000)] + [i * 1.1 for i in range(50000)] + [float(i) for i in range(1, 100000, 7)]
    n = 0
    for f in tests:
        if not math.isfinite(f) or f == 0: continue
        n += 1
        if to_dec(abs(f)) != py_digits(abs(f)):
            bad += 1
            if bad < 5: print("MISMATCH", repr(f), to_dec(abs(f)), py_digits(abs(f)))
    print("tested", n, "mismatches", bad)

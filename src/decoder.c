/* yjson.loads: a strict JSON parser that builds Python objects directly.

   This file is independent of the encoder (src/yjson.mojo and src/python_api.c):
   it shares no code or state with dumps, and yjson_install only registers the
   function it exports.

   The parser follows orjson's rules: only RFC 8259 JSON (no NaN, Infinity,
   comments, trailing commas or byte order mark), valid UTF-8 without lone
   surrogates, integers within [-2^63, 2^64) (larger ones become floats), and at
   most 1024 nested containers. Errors are reported as yjson.JSONDecodeError
   (the class defined in _yjson_support) with the character position at which
   the parser stopped, as orjson reports it. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <emmintrin.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if PY_VERSION_HEX < 0x030C0000
#include <structmember.h>  /* T_OBJECT_EX before 3.12 */
#define Py_T_OBJECT_EX T_OBJECT_EX
#define Py_READONLY READONLY
#endif

#include "decoder_powers.h"

#if PY_VERSION_HEX < 0x030C0000
/* 3.12 API over the 3.11 fetch/restore pair, used while clearing the plan cache. */
static PyObject *PyErr_GetRaisedException(void) {
    PyObject *type, *value, *traceback;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    if (value && traceback) PyException_SetTraceback(value, traceback);
    Py_XDECREF(type);
    Py_XDECREF(traceback);
    return value;
}
static void PyErr_SetRaisedException(PyObject *value) {
    if (value) PyErr_Restore(Py_NewRef(Py_TYPE(value)), value, NULL);
}
#endif

#define MAX_DEPTH 1024
#define KEY_CACHE_SLOTS 2048       /* power of two */
#define KEY_CACHE_MAX_LENGTH 64
#define INLINE_STACK 128

static const char *const MSG_EOF = "unexpected end of data";
static const char *const MSG_VALUE = "unexpected character, expected a JSON value";
static const char *const MSG_UTF8 = "str is not valid UTF-8: surrogates not allowed";

typedef struct Plan Plan;     /* a compiled loads(type=...) annotation, below */

typedef struct {
    const unsigned char *start, *cur, *end;
    int depth;
    const char *error;            /* set by fail(); NULL when a Python exception is pending instead */
    Py_ssize_t error_at;          /* byte offset of the error */
    char message[200];            /* storage for formatted error messages */
    PyObject **stack;             /* values of the arrays being built, innermost last */
    Py_ssize_t stack_len, stack_cap;
    unsigned char *scratch;       /* unescaped string bytes */
    Py_ssize_t scratch_cap;
    PyObject *inline_stack[INLINE_STACK];
} Parser;

static PyObject *decode_error_type;              /* yjson.JSONDecodeError */
static PyObject *key_cache[KEY_CACHE_SLOTS];     /* recently seen ASCII object keys */

static PyObject *fail(Parser *p, const char *message, const unsigned char *at) {
    p->error = message;
    p->error_at = at - p->start;
    return NULL;
}

static PyObject *failf(Parser *p, const unsigned char *at, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(p->message, sizeof p->message, format, args);
    va_end(args);
    return fail(p, p->message, at);
}

/* --- scanning ------------------------------------------------------------ */

static inline int is_space(unsigned char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
static inline int is_digit(unsigned char c) { return (unsigned)(c - '0') < 10; }

static inline const unsigned char *skip_space(const unsigned char *c, const unsigned char *end) {
    while (c < end && is_space(*c)) c++;
    return c;
}

/* First byte that is '"', '\\' or a control character, or `end`. With `strict`,
   bytes >= 0x80 stop the scan too, so a string that passes is plain ASCII. */
static inline const unsigned char *scan(const unsigned char *c, const unsigned char *end, int strict) {
    const __m128i quote = _mm_set1_epi8('"'), backslash = _mm_set1_epi8('\\'), space = _mm_set1_epi8(0x20), max_control = _mm_set1_epi8(0x1F);
    while (end - c >= 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)c);
        __m128i special = _mm_or_si128(_mm_cmpeq_epi8(v, quote), _mm_cmpeq_epi8(v, backslash));
        /* Signed compare: bytes >= 0x80 are negative, so "< 0x20" flags them with the controls. */
        if (strict) special = _mm_or_si128(special, _mm_cmplt_epi8(v, space));
        else special = _mm_or_si128(special, _mm_cmpeq_epi8(_mm_min_epu8(v, max_control), v));
        int mask = _mm_movemask_epi8(special);
        if (mask) return c + __builtin_ctz(mask);
        c += 16;
    }
    while (c < end) {
        unsigned char b = *c;
        if (b == '"' || b == '\\' || b < 0x20 || (strict && b >= 0x80)) break;
        c++;
    }
    return c;
}

/* --- strings -------------------------------------------------------------- */

static PyObject *ascii_string(const unsigned char *data, Py_ssize_t length) {
    PyObject *result = PyUnicode_New(length, 127);
    if (result && length) memcpy(PyUnicode_1BYTE_DATA(result), data, (size_t)length);
    return result;
}

static signed char hex_values[256];  /* digit value of each byte, or -1; filled by yjson_install_loads */

/* Reads the four hex digits after "\u" at `c` (which points at the backslash). */
static int hex4(const unsigned char *c, const unsigned char *end, uint32_t *value, int *truncated) {
    uint32_t result = 0;
    for (int i = 2; i < 6; i++) {
        if (c + i >= end) { *truncated = 1; return 0; }
        int digit = hex_values[c[i]];
        if (digit < 0) return 0;
        result = result << 4 | (uint32_t)digit;
    }
    *value = result;
    return 1;
}

static inline unsigned char *put_utf8(unsigned char *out, uint32_t cp) {
    if (cp < 0x80) { *out++ = (unsigned char)cp; }
    else if (cp < 0x800) { *out++ = (unsigned char)(0xC0 | cp >> 6); *out++ = (unsigned char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        *out++ = (unsigned char)(0xE0 | cp >> 12); *out++ = (unsigned char)(0x80 | (cp >> 6 & 0x3F)); *out++ = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        *out++ = (unsigned char)(0xF0 | cp >> 18); *out++ = (unsigned char)(0x80 | (cp >> 12 & 0x3F));
        *out++ = (unsigned char)(0x80 | (cp >> 6 & 0x3F)); *out++ = (unsigned char)(0x80 | (cp & 0x3F));
    }
    return out;
}

/* Continues a string whose bytes [start, special) are plain and whose byte at
   `special` is an escape, a control character or the end of input. Escapes are
   resolved into UTF-8 in the scratch buffer (never longer than the input), which
   CPython's decoder then validates. */
static PyObject *string_slow(Parser *p, const unsigned char *start, const unsigned char *c) {
    const unsigned char *end = p->end;
    if (p->scratch_cap < end - start) {
        unsigned char *fresh = PyMem_Realloc(p->scratch, (size_t)(end - start));
        if (!fresh) return PyErr_NoMemory();
        p->scratch = fresh;
        p->scratch_cap = end - start;
    }
    unsigned char *out = p->scratch;
    memcpy(out, start, (size_t)(c - start));
    out += c - start;
    for (;;) {
        if (c >= end) return fail(p, MSG_EOF, end);
        unsigned char b = *c;
        if (b == '"') { c++; break; }
        if (b == '\\') {
            if (c + 1 >= end) return fail(p, MSG_EOF, end);
            switch (c[1]) {
            case '"': *out++ = '"'; c += 2; break;
            case '\\': *out++ = '\\'; c += 2; break;
            case '/': *out++ = '/'; c += 2; break;
            case 'b': *out++ = '\b'; c += 2; break;
            case 'f': *out++ = '\f'; c += 2; break;
            case 'n': *out++ = '\n'; c += 2; break;
            case 'r': *out++ = '\r'; c += 2; break;
            case 't': *out++ = '\t'; c += 2; break;
            case 'u': {
                uint32_t cp, low;
                int truncated = 0;
                if (!hex4(c, end, &cp, &truncated)) return truncated ? fail(p, MSG_EOF, end) : fail(p, "invalid escaped sequence in string", c);
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (c + 8 > end) {  /* the input ends within two bytes: either mid-escape or without "\u" */
                        int prefix = c + 6 >= end || (c[6] == '\\' && c + 7 >= end);
                        return prefix ? fail(p, MSG_EOF, end) : fail(p, "no low surrogate in string", c);
                    }
                    if (c[6] != '\\' || c[7] != 'u') return fail(p, "no low surrogate in string", c);
                    if (!hex4(c + 6, end, &low, &truncated)) return truncated ? fail(p, MSG_EOF, end) : fail(p, "invalid low surrogate in string", c);
                    if (low < 0xDC00 || low > 0xDFFF) return fail(p, "invalid low surrogate in string", c);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    c += 12;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail(p, "lone low surrogate in string", c);
                } else {
                    c += 6;
                }
                out = put_utf8(out, cp);
                break;
            }
            default: return fail(p, "invalid escaped sequence in string", c);
            }
            continue;
        }
        if (b < 0x20) return fail(p, "unexpected control character in string", c);
        /* A run of ordinary bytes, non-ASCII included. */
        const unsigned char *run = scan(c, end, 0);
        memcpy(out, c, (size_t)(run - c));
        out += run - c;
        c = run;
    }
    PyObject *result = PyUnicode_DecodeUTF8((const char *)p->scratch, out - p->scratch, NULL);
    if (!result) {
        if (!PyErr_ExceptionMatches(PyExc_UnicodeDecodeError)) return NULL;
        PyErr_Clear();
        return fail(p, MSG_UTF8, start - 1);
    }
    p->cur = c;
    return result;
}

/* Finishes a string whose opening quote is at start - 1 and whose first special
   byte (from a strict scan) is at `special`. */
static PyObject *string_from(Parser *p, const unsigned char *start, const unsigned char *special) {
    const unsigned char *end = p->end;
    if (special < end && *special == '"') {
        p->cur = special + 1;
        return ascii_string(start, special - start);
    }
    if (special < end && *special >= 0x80) {
        const unsigned char *close = scan(special, end, 0);
        if (close < end && *close == '"') {
            PyObject *result = PyUnicode_DecodeUTF8((const char *)start, close - start, NULL);
            if (!result) {
                if (!PyErr_ExceptionMatches(PyExc_UnicodeDecodeError)) return NULL;
                PyErr_Clear();
                return fail(p, MSG_UTF8, start - 1);
            }
            p->cur = close + 1;
            return result;
        }
        special = close;
    }
    return string_slow(p, start, special);
}

static PyObject *parse_string(Parser *p) {
    const unsigned char *start = p->cur + 1;
    return string_from(p, start, scan(start, p->end, 1));
}

static inline uint64_t key_hash(const unsigned char *s, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ n;
    while (n >= 8) {
        uint64_t w;
        memcpy(&w, s, 8);
        h = (h ^ w) * 0xFF51AFD7ED558CCDULL;
        h ^= h >> 29;
        s += 8;
        n -= 8;
    }
    if (n) {
        uint64_t w = 0;
        memcpy(&w, s, n);
        h = (h ^ w) * 0xFF51AFD7ED558CCDULL;
        h ^= h >> 29;
    }
    return h ^ h >> 32;
}

/* Object keys: short ASCII keys without escapes come from a cache, so repeated
   keys share one str object and its cached hash. */
static PyObject *parse_key(Parser *p) {
    const unsigned char *start = p->cur + 1, *end = p->end;
    const unsigned char *special = scan(start, end, 1);
    Py_ssize_t length = special - start;
    if (special >= end || *special != '"' || length > KEY_CACHE_MAX_LENGTH) return string_from(p, start, special);
    PyObject **slot = &key_cache[key_hash(start, (size_t)length) & (KEY_CACHE_SLOTS - 1)];
    PyObject *cached = *slot;
    if (cached && PyUnicode_GET_LENGTH(cached) == length && memcmp(PyUnicode_1BYTE_DATA(cached), start, (size_t)length) == 0) {
        p->cur = special + 1;
        return Py_NewRef(cached);
    }
    PyObject *key = ascii_string(start, length);
    if (!key) return NULL;
    Py_XSETREF(*slot, Py_NewRef(key));
    p->cur = special + 1;
    return key;
}

/* --- numbers -------------------------------------------------------------- */

/* Eight ASCII digits read as one little-endian word, and their value. */
static inline int eight_digits(const unsigned char *c, const unsigned char *end, uint64_t *word) {
    if (end - c < 8) return 0;
    uint64_t v;
    memcpy(&v, c, 8);
    *word = v;
    return ((v & 0xF0F0F0F0F0F0F0F0ULL) | (((v + 0x0606060606060606ULL) & 0xF0F0F0F0F0F0F0F0ULL) >> 4)) == 0x3333333333333333ULL;
}

static inline uint32_t eight_digits_value(uint64_t v) {
    v -= 0x3030303030303030ULL;
    v = v * 10 + (v >> 8);
    v = ((v & 0x000000FF000000FFULL) * (100 + (1000000ULL << 32)) + ((v >> 16 & 0x000000FF000000FFULL) * (1 + (10000ULL << 32)))) >> 32;
    return (uint32_t)v;
}

static const double exact_powers_of_ten[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

/* Eisel-Lemire: the correctly rounded double nearest to mantissa * 10^exponent for a
   mantissa below 2^64, as in fast_float's compute_float. Mushtak and Lemire ("Fast Number
   Parsing Without Fallback", 2023) prove the two-word product always suffices. Returns 0
   only when the result is infinite. */
static int eisel_lemire(uint64_t mantissa, int exponent, double *out) {
    if (mantissa == 0 || exponent < POWER_OF_FIVE_SMALLEST) { *out = 0.0; return 1; }
    if (exponent > POWER_OF_FIVE_LARGEST) return 0;
    int lz = __builtin_clzll(mantissa);
    uint64_t w = mantissa << lz;
    const uint64_t *power = powers_of_five[exponent - POWER_OF_FIVE_SMALLEST];
    unsigned __int128 product = (unsigned __int128)w * power[0];
    uint64_t high = (uint64_t)(product >> 64), low = (uint64_t)product;
    if ((high & 0x1FF) == 0x1FF) {  /* 2^(64 - 52 - 3) - 1: the truncated product may not decide the rounding */
        uint64_t second_high = (uint64_t)(((unsigned __int128)w * power[1]) >> 64);
        low += second_high;
        if (second_high > low) high++;
    }
    int upper_bit = (int)(high >> 63);
    int shift = upper_bit + 64 - 52 - 3;
    uint64_t m = high >> shift;
    int power2 = (((152170 + 65536) * exponent) >> 16) + 63 + upper_bit - lz + 1023;
    if (power2 <= 0) {  /* subnormal */
        if (-power2 + 1 >= 64) { *out = 0.0; return 1; }
        m >>= -power2 + 1;
        m += m & 1;
        m >>= 1;
        power2 = m < ((uint64_t)1 << 52) ? 0 : 1;
    } else {
        /* Round half to even when the product is exactly halfway (only possible for small exponents). */
        if (low <= 1 && exponent >= -4 && exponent <= 23 && (m & 3) == 1 && (m << shift) == high) m &= ~(uint64_t)1;
        m += m & 1;
        m >>= 1;
        if (m >= ((uint64_t)2 << 52)) { m = (uint64_t)1 << 52; power2++; }
        m &= ~((uint64_t)1 << 52);
        if (power2 >= 0x7FF) return 0;
    }
    uint64_t bits = m | (uint64_t)power2 << 52;
    memcpy(out, &bits, sizeof bits);
    return 1;
}

static PyObject *float_from_text(Parser *p, const unsigned char *start, const unsigned char *stop) {
    char buffer[64], *text = buffer;
    size_t length = (size_t)(stop - start);
    if (length >= sizeof buffer) {
        text = PyMem_Malloc(length + 1);
        if (!text) return PyErr_NoMemory();
    }
    memcpy(text, start, length);
    text[length] = 0;
    char *tail = NULL;
    double value = PyOS_string_to_double(text, &tail, NULL);
    if (text != buffer) PyMem_Free(text);
    if (value == -1.0 && PyErr_Occurred()) return NULL;
    if (isinf(value)) return fail(p, "number is infinity when parsed as double", start);
    return PyFloat_FromDouble(value);
}

static PyObject *parse_number(Parser *p) {
    const unsigned char *start = p->cur, *c = start, *end = p->end;
    int negative = 0;
    if (*c == '-') {
        negative = 1;
        if (++c >= end) return fail(p, MSG_EOF, end);
    }
    uint64_t mantissa = 0;
    int digits = 0, inexact = 0, is_float = 0, exponent = 0;
    if (*c == '0') {
        c++;
        if (c < end && is_digit(*c)) return fail(p, "number with leading zero is not allowed", start);
    } else if (is_digit(*c)) {
        uint64_t word;
        while (digits + 8 <= 19 && eight_digits(c, end, &word)) {
            mantissa = mantissa * 100000000 + eight_digits_value(word);
            digits += 8;
            c += 8;
        }
        while (c < end && is_digit(*c)) {
            unsigned d = *c - '0';
            if (digits < 19) mantissa = mantissa * 10 + d;
            else if (digits == 19 && mantissa <= (UINT64_MAX - d) / 10) mantissa = mantissa * 10 + d;
            else inexact = 1;
            digits++;
            c++;
        }
    } else {
        return fail(p, negative ? "no digit after sign" : MSG_VALUE, start);
    }
    if (c < end && *c == '.') {
        is_float = 1;
        if (++c >= end) return fail(p, MSG_EOF, end);
        if (!is_digit(*c)) return fail(p, "unexpected character in number, expected a digit", c);
        uint64_t word;
        while (digits + 8 <= 19 && eight_digits(c, end, &word)) {
            mantissa = mantissa * 100000000 + eight_digits_value(word);
            exponent -= 8;
            digits += 8;
            c += 8;
        }
        while (c < end && is_digit(*c)) {
            unsigned d = *c - '0';
            if (digits < 19) { mantissa = mantissa * 10 + d; exponent--; digits++; }
            else inexact = 1;
            c++;
        }
    }
    if (c < end && (*c == 'e' || *c == 'E')) {
        is_float = 1;
        if (++c >= end) return fail(p, MSG_EOF, end);
        int sign = 1;
        if (*c == '+' || *c == '-') {
            sign = *c == '-' ? -1 : 1;
            if (++c >= end) return fail(p, MSG_EOF, end);
        }
        if (!is_digit(*c)) return fail(p, "unexpected character in number, expected a digit", c);
        int e = 0;
        do {
            if (e < 100000) e = e * 10 + (*c - '0');
            c++;
        } while (c < end && is_digit(*c));
        exponent += sign * e;
    }
    p->cur = c;
    if (!is_float && !inexact) {
        if (!negative) return mantissa <= (uint64_t)LLONG_MAX ? PyLong_FromLongLong((long long)mantissa) : PyLong_FromUnsignedLongLong(mantissa);
        if (mantissa < (uint64_t)1 << 63) return PyLong_FromLongLong(-(long long)mantissa);
        if (mantissa == (uint64_t)1 << 63) return PyLong_FromLongLong(LLONG_MIN);
    }
    if (!inexact) {
        double value;
        if (mantissa <= ((uint64_t)1 << 53) && exponent >= -22 && exponent <= 22) {
            /* Clinger's fast path: the mantissa and the power of ten are exact doubles, so
               one multiplication or division rounds correctly. */
            value = (double)mantissa;
            value = exponent < 0 ? value / exact_powers_of_ten[-exponent] : value * exact_powers_of_ten[exponent];
            return PyFloat_FromDouble(negative ? -value : value);
        }
        if (eisel_lemire(mantissa, exponent, &value)) return PyFloat_FromDouble(negative ? -value : value);
    }
    /* More than 19 significant digits, or an infinite result: CPython's strtod decides. */
    return float_from_text(p, start, c);
}

/* --- containers ----------------------------------------------------------- */

static int push(Parser *p, PyObject *value) {
    if (p->stack_len == p->stack_cap) {
        Py_ssize_t cap = p->stack_cap * 2;
        PyObject **fresh = PyMem_Malloc((size_t)cap * sizeof *fresh);
        if (!fresh) { Py_DECREF(value); PyErr_NoMemory(); return 0; }
        memcpy(fresh, p->stack, (size_t)p->stack_len * sizeof *fresh);
        if (p->stack != p->inline_stack) PyMem_Free(p->stack);
        p->stack = fresh;
        p->stack_cap = cap;
    }
    p->stack[p->stack_len++] = value;
    return 1;
}

static PyObject *parse_value(Parser *p);
static PyObject *parse_typed(Parser *p, const Plan *plan);

/* `item` types the elements when loads() was given a type; NULL parses them untyped. */
static PyObject *parse_array(Parser *p, const Plan *item) {
    const unsigned char *end = p->end;
    if (++p->depth > MAX_DEPTH) return fail(p, "recursion limit exceeded", p->cur);
    const unsigned char *c = skip_space(p->cur + 1, end);
    if (c < end && *c == ']') {
        p->cur = c + 1;
        p->depth--;
        return PyList_New(0);
    }
    Py_ssize_t base = p->stack_len;
    for (;;) {
        p->cur = c;
        PyObject *value = item ? parse_typed(p, item) : parse_value(p);
        if (!value || !push(p, value)) return NULL;  /* loads() releases the stack */
        c = skip_space(p->cur, end);
        if (c >= end) return fail(p, MSG_EOF, end);
        if (*c == ',') {
            const unsigned char *comma = c;
            c = skip_space(c + 1, end);
            if (c < end && *c == ']') return fail(p, "trailing comma is not allowed", comma);
            continue;
        }
        if (*c == ']') break;
        return fail(p, "unexpected character, expected ',' or ']'", c);
    }
    Py_ssize_t count = p->stack_len - base;
    PyObject *list = PyList_New(count);
    if (!list) return NULL;
    for (Py_ssize_t i = 0; i < count; i++) PyList_SET_ITEM(list, i, p->stack[base + i]);  /* moves the references */
    p->stack_len = base;
    p->cur = c + 1;
    p->depth--;
    return list;
}

static PyObject *parse_object(Parser *p, const Plan *item) {
    const unsigned char *end = p->end;
    if (++p->depth > MAX_DEPTH) return fail(p, "recursion limit exceeded", p->cur);
    const unsigned char *c = skip_space(p->cur + 1, end);
    PyObject *dict = PyDict_New();
    if (!dict) return NULL;
    if (c < end && *c == '}') {
        p->cur = c + 1;
        p->depth--;
        return dict;
    }
    for (;;) {
        if (c >= end) { fail(p, MSG_EOF, end); break; }
        if (*c != '"') { fail(p, "unexpected character, expected a string key", c); break; }
        p->cur = c;
        PyObject *key = parse_key(p);
        if (!key) break;
        c = skip_space(p->cur, end);
        if (c >= end) { Py_DECREF(key); fail(p, MSG_EOF, end); break; }
        if (*c != ':') { Py_DECREF(key); fail(p, "unexpected character, expected ':' after key", c); break; }
        p->cur = c + 1;
        PyObject *value = item ? parse_typed(p, item) : parse_value(p);
        if (!value) { Py_DECREF(key); break; }
        int status = PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
        if (status < 0) break;
        c = skip_space(p->cur, end);
        if (c >= end) { fail(p, MSG_EOF, end); break; }
        if (*c == ',') {
            const unsigned char *comma = c;
            c = skip_space(c + 1, end);
            if (c < end && *c == '}') { fail(p, "trailing comma is not allowed", comma); break; }
            continue;
        }
        if (*c == '}') {
            p->cur = c + 1;
            p->depth--;
            return dict;
        }
        fail(p, "unexpected character, expected ',' or '}'", c);
        break;
    }
    Py_DECREF(dict);
    return NULL;
}

static PyObject *parse_literal(Parser *p, const char *word, int length, PyObject *value) {
    Py_ssize_t available = p->end - p->cur;
    if (available >= length && memcmp(p->cur, word, (size_t)length) == 0) {
        p->cur += length;
        return Py_NewRef(value);
    }
    if (available < length && memcmp(p->cur, word, (size_t)available) == 0) return fail(p, MSG_EOF, p->end);
    return fail(p, MSG_VALUE, p->cur);
}

static PyObject *parse_value(Parser *p) {
    const unsigned char *c = skip_space(p->cur, p->end);
    p->cur = c;
    if (c >= p->end) return fail(p, MSG_EOF, p->end);
    switch (*c) {
    case '"': return parse_string(p);
    case '{': return parse_object(p, NULL);
    case '[': return parse_array(p, NULL);
    case 't': return parse_literal(p, "true", 4, Py_True);
    case 'f': return parse_literal(p, "false", 5, Py_False);
    case 'n': return parse_literal(p, "null", 4, Py_None);
    case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        return parse_number(p);
    default: return fail(p, MSG_VALUE, c);
    }
}


/* --- loads(type=...) ------------------------------------------------------ */

/* A Plan is the compiled form of an annotation: Any, int, float, str, bool, None,
   Optional[X], list[X], dict[str, X] or a dataclass. Plans are built once per
   annotation from _yjson_support.describe_type and cached for the life of the
   process, so a dataclass may refer to itself. Dataclass instances are allocated
   directly and their fields written in place: neither __new__ nor __init__ runs,
   __post_init__ does. */
enum { PLAN_ANY, PLAN_INT, PLAN_FLOAT, PLAN_STR, PLAN_BOOL, PLAN_NONE, PLAN_OPTIONAL, PLAN_LIST, PLAN_DICT, PLAN_DATACLASS };

typedef struct {
    PyObject *name;            /* attribute name, interned (also the JSON key) */
    Plan *plan;
    PyObject *default_value;   /* the default, or the factory for default_kind 2 */
    int default_kind;          /* 0 required, 1 value, 2 factory, 3 left unset when absent */
    Py_ssize_t slot;           /* byte offset of a __slots__ member, or -1 to set the attribute */
    uint64_t hash;             /* of the ASCII key bytes */
    Py_ssize_t length;
    const unsigned char *bytes;
} Field;

struct Plan {
    int kind;
    char *name;                /* for error messages */
    Plan *item;                /* Optional, list and dict */
    PyTypeObject *cls;         /* dataclass */
    Field *fields;
    int nfields, seen_words;
    int *table, mask;          /* open-addressed ASCII key -> field index, or -1 */
    PyObject *key_index;       /* dict: key str -> field index, for keys with escapes */
    int post_init;
};

static PyObject *plan_cache;      /* annotation -> capsule owning its Plan */
static PyObject *plan_batch;      /* annotations added to the cache by the build in progress */
static int plan_depth;            /* nesting of plan_for calls during a build */
static PyObject *describe_type;   /* _yjson_support.describe_type */
static PyObject *post_init_name;  /* "__post_init__" */

static void free_plan(PyObject *capsule) {
    Plan *plan = PyCapsule_GetPointer(capsule, "yjson.plan");
    if (!plan) return;
    for (int i = 0; i < plan->nfields; i++) {
        Py_XDECREF(plan->fields[i].name);
        Py_XDECREF(plan->fields[i].default_value);
    }
    PyMem_Free(plan->fields);
    PyMem_Free(plan->table);
    PyMem_Free(plan->name);
    Py_XDECREF(plan->cls);
    Py_XDECREF(plan->key_index);
    PyMem_Free(plan);
}

static char *copy_name(const char *text) {
    char *copy = PyMem_Malloc(strlen(text) + 1);
    if (copy) strcpy(copy, text);
    return copy;
}

/* The byte offset of `name` when the class stores it in a __slots__ member, else -1. */
static Py_ssize_t slot_offset(PyTypeObject *cls, PyObject *name) {
    PyObject *descr = PyObject_GetAttr((PyObject *)cls, name);
    if (!descr) { PyErr_Clear(); return -1; }
    Py_ssize_t offset = -1;
    if (Py_TYPE(descr) == &PyMemberDescr_Type) {
        PyMemberDef *member = ((PyMemberDescrObject *)descr)->d_member;
        if (member->type == Py_T_OBJECT_EX && !(member->flags & Py_READONLY) && member->offset >= (Py_ssize_t)sizeof(PyObject)
            && member->offset + (Py_ssize_t)sizeof(PyObject *) <= cls->tp_basicsize) offset = member->offset;
    }
    Py_DECREF(descr);
    return offset;
}

static Plan *plan_for(PyObject *annotation);

static int build_dataclass_plan(Plan *plan, PyObject *description) {
    PyObject *cls = PyTuple_GET_ITEM(description, 1), *fields = PyTuple_GET_ITEM(description, 2);
    if (!PyType_Check(cls) || !PyTuple_Check(fields)) { PyErr_SetString(PyExc_TypeError, "invalid dataclass description"); return -1; }
    plan->cls = (PyTypeObject *)Py_NewRef(cls);
    plan->name = copy_name(plan->cls->tp_name);
    plan->post_init = PyObject_IsTrue(PyTuple_GET_ITEM(description, 3));
    plan->key_index = PyDict_New();
    Py_ssize_t count = PyTuple_GET_SIZE(fields);
    plan->fields = PyMem_Calloc((size_t)(count ? count : 1), sizeof(Field));
    if (!plan->name || !plan->key_index || !plan->fields) { PyErr_NoMemory(); return -1; }
    int size = 4;
    while (size < 2 * count) size *= 2;
    plan->table = PyMem_Malloc((size_t)size * sizeof(int));
    if (!plan->table) { PyErr_NoMemory(); return -1; }
    for (int i = 0; i < size; i++) plan->table[i] = -1;
    plan->mask = size - 1;
    plan->seen_words = (int)((count + 63) / 64);
    if (!plan->seen_words) plan->seen_words = 1;
    for (Py_ssize_t i = 0; i < count; i++) {
        PyObject *entry = PyTuple_GET_ITEM(fields, i);
        if (!PyTuple_Check(entry) || PyTuple_GET_SIZE(entry) != 4 || !PyUnicode_Check(PyTuple_GET_ITEM(entry, 0))) {
            PyErr_SetString(PyExc_TypeError, "invalid dataclass field description");
            return -1;
        }
        Field *field = &plan->fields[i];
        plan->nfields = (int)(i + 1);
        field->name = Py_NewRef(PyTuple_GET_ITEM(entry, 0));
        PyUnicode_InternInPlace(&field->name);
        field->default_kind = (int)PyLong_AsLong(PyTuple_GET_ITEM(entry, 2));
        if (field->default_kind == -1 && PyErr_Occurred()) return -1;
        field->default_value = Py_NewRef(PyTuple_GET_ITEM(entry, 3));
        field->slot = slot_offset(plan->cls, field->name);
        field->plan = plan_for(PyTuple_GET_ITEM(entry, 1));
        if (!field->plan) return -1;
        PyObject *index = PyLong_FromSsize_t(i);
        if (!index) return -1;
        int status = PyDict_SetItem(plan->key_index, field->name, index);
        Py_DECREF(index);
        if (status < 0) return -1;
        if (PyUnicode_IS_ASCII(field->name)) {
            field->bytes = PyUnicode_1BYTE_DATA(field->name);
            field->length = PyUnicode_GET_LENGTH(field->name);
            field->hash = key_hash(field->bytes, (size_t)field->length);
            int at = (int)(field->hash & (uint64_t)plan->mask);
            while (plan->table[at] >= 0) at = (at + 1) & plan->mask;
            plan->table[at] = (int)i;
        }
    }
    return 0;
}

static int build_plan(Plan *plan, PyObject *annotation) {
    PyObject *description = PyObject_CallOneArg(describe_type, annotation);
    if (!description) return -1;
    int status = -1;
    if (!PyTuple_Check(description) || PyTuple_GET_SIZE(description) < 1 || !PyUnicode_Check(PyTuple_GET_ITEM(description, 0))) {
        PyErr_SetString(PyExc_TypeError, "invalid type description");
        goto done;
    }
    const char *kind = PyUnicode_AsUTF8(PyTuple_GET_ITEM(description, 0));
    if (!kind) goto done;
    static const struct { const char *kind; int code; } scalars[] = {
        {"any", PLAN_ANY}, {"int", PLAN_INT}, {"float", PLAN_FLOAT}, {"str", PLAN_STR}, {"bool", PLAN_BOOL}, {"none", PLAN_NONE},
    };
    for (size_t i = 0; i < sizeof scalars / sizeof *scalars; i++) {
        if (strcmp(kind, scalars[i].kind) == 0) {
            plan->kind = scalars[i].code;
            plan->name = copy_name(scalars[i].code == PLAN_NONE ? "None" : kind);
            status = plan->name ? 0 : -1;
            if (status < 0) PyErr_NoMemory();
            goto done;
        }
    }
    if (strcmp(kind, "dataclass") == 0 && PyTuple_GET_SIZE(description) == 4) {
        plan->kind = PLAN_DATACLASS;
        status = build_dataclass_plan(plan, description);
        goto done;
    }
    if (PyTuple_GET_SIZE(description) != 2) { PyErr_SetString(PyExc_TypeError, "invalid type description"); goto done; }
    const char *label;
    if (strcmp(kind, "optional") == 0) { plan->kind = PLAN_OPTIONAL; label = "Optional[%s]"; }
    else if (strcmp(kind, "list") == 0) { plan->kind = PLAN_LIST; label = "list[%s]"; }
    else if (strcmp(kind, "dict") == 0) { plan->kind = PLAN_DICT; label = "dict[str, %s]"; }
    else { PyErr_Format(PyExc_TypeError, "unknown type description %s", kind); goto done; }
    plan->item = plan_for(PyTuple_GET_ITEM(description, 1));
    if (!plan->item) goto done;
    size_t length = strlen(label) + strlen(plan->item->name) + 1;
    plan->name = PyMem_Malloc(length);
    if (!plan->name) { PyErr_NoMemory(); goto done; }
    snprintf(plan->name, length, label, plan->item->name);
    status = 0;
done:
    Py_DECREF(description);
    return status;
}

/* The plan for an annotation, from the cache or newly built. The cache entry is
   created before the plan is filled, so self-referencing dataclasses resolve to it.
   The plans one top-level build adds can only point at each other, never at older
   entries' dependents, so when the build fails they are all removed together and
   plans already in use elsewhere are untouched. */
static Plan *plan_for(PyObject *annotation) {
    PyObject *capsule = PyDict_GetItemWithError(plan_cache, annotation);
    if (capsule) return PyCapsule_GetPointer(capsule, "yjson.plan");
    if (PyErr_Occurred()) return NULL;
    if (plan_depth == 0) {
        Py_XSETREF(plan_batch, PyList_New(0));
        if (!plan_batch) return NULL;
    }
    Plan *plan = PyMem_Calloc(1, sizeof(Plan));
    if (!plan) { PyErr_NoMemory(); return NULL; }
    capsule = PyCapsule_New(plan, "yjson.plan", free_plan);
    if (!capsule) { PyMem_Free(plan); return NULL; }
    int status = PyDict_SetItem(plan_cache, annotation, capsule);
    Py_DECREF(capsule);
    if (status < 0 || PyList_Append(plan_batch, annotation) < 0) return NULL;
    plan_depth++;
    status = build_plan(plan, annotation);
    plan_depth--;
    if (status == 0) {
        if (plan_depth == 0) Py_CLEAR(plan_batch);
        return plan;
    }
    if (plan_depth == 0) {
        PyObject *error = PyErr_GetRaisedException();
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(plan_batch); i++) {
            if (PyDict_DelItem(plan_cache, PyList_GET_ITEM(plan_batch, i)) < 0) PyErr_Clear();
        }
        Py_CLEAR(plan_batch);
        PyErr_SetRaisedException(error);
    }
    return NULL;
}

static const char *token_name(const unsigned char *c, const unsigned char *end) {
    if (c >= end) return "end of data";
    switch (*c) {
    case '{': return "object";
    case '[': return "array";
    case '"': return "str";
    case 't': case 'f': return "bool";
    case 'n': return "null";
    default: return "number";
    }
}

static PyObject *type_error(Parser *p, const Plan *plan, const unsigned char *at, const char *got) {
    return failf(p, at, "expected %s, got %s", plan->name, got ? got : token_name(at, p->end));
}

static int set_field(PyObject *obj, const Field *field, PyObject *value) {
    if (field->slot >= 0) {
        Py_XSETREF(*(PyObject **)((char *)obj + field->slot), Py_NewRef(value));
        return 0;
    }
    return PyObject_GenericSetAttr(obj, field->name, value);
}

static PyObject *parse_dataclass(Parser *p, const Plan *plan) {
    const unsigned char *end = p->end;
    if (++p->depth > MAX_DEPTH) return fail(p, "recursion limit exceeded", p->cur);
    const unsigned char *c = skip_space(p->cur + 1, end);
    PyObject *obj = plan->cls->tp_alloc(plan->cls, 0);
    if (!obj) return NULL;
#ifdef YJSON_EXPERIMENT_UNTRACK
    PyObject_GC_UnTrack(obj);  /* measurement only: not safe for dataclasses, see the notes */
#endif
    uint64_t seen[plan->seen_words];
    memset(seen, 0, sizeof seen);
    int expected = 0;  /* documents usually list the fields in declaration order */
    if (c < end && *c == '}') {
        c++;
        goto defaults;
    }
    for (;;) {
        if (c >= end) { fail(p, MSG_EOF, end); break; }
        if (*c != '"') { fail(p, "unexpected character, expected a string key", c); break; }
        /* Match the key against the fields: the field expected next by a direct compare,
           other plain ASCII keys through the hash table, keys with escapes or non-ASCII
           bytes through the dict. */
        const unsigned char *start = c + 1, *special = scan(start, end, 1);
        int index = -1;
        if (special < end && *special == '"') {
            Py_ssize_t length = special - start;
            const Field *next = &plan->fields[expected];
            if (plan->nfields && next->length == length && memcmp(next->bytes, start, (size_t)length) == 0) {
                index = expected;
            } else {
                uint64_t hash = key_hash(start, (size_t)length);
                int at = (int)(hash & (uint64_t)plan->mask);
                for (; plan->table[at] >= 0; at = (at + 1) & plan->mask) {
                    const Field *field = &plan->fields[plan->table[at]];
                    if (field->hash == hash && field->length == length && memcmp(field->bytes, start, (size_t)length) == 0) {
                        index = plan->table[at];
                        break;
                    }
                }
            }
            p->cur = special + 1;
        } else {
            PyObject *key = string_from(p, start, special);
            if (!key) break;
            PyObject *found = PyDict_GetItemWithError(plan->key_index, key);
            Py_DECREF(key);
            if (found) index = (int)PyLong_AsLong(found);
            else if (PyErr_Occurred()) break;
        }
        c = skip_space(p->cur, end);
        if (c >= end) { fail(p, MSG_EOF, end); break; }
        if (*c != ':') { fail(p, "unexpected character, expected ':' after key", c); break; }
        p->cur = c + 1;
        if (index >= 0) {
            const Field *field = &plan->fields[index];
            PyObject *value = parse_typed(p, field->plan);
            if (!value) break;
            int status = set_field(obj, field, value);
            Py_DECREF(value);
            if (status < 0) break;
            seen[index / 64] |= (uint64_t)1 << (index % 64);
            expected = index + 1 < plan->nfields ? index + 1 : 0;
        } else {
            PyObject *value = parse_value(p);  /* an unknown key: its value is checked and dropped */
            if (!value) break;
            Py_DECREF(value);
        }
        c = skip_space(p->cur, end);
        if (c >= end) { fail(p, MSG_EOF, end); break; }
        if (*c == ',') {
            const unsigned char *comma = c;
            c = skip_space(c + 1, end);
            if (c < end && *c == '}') { fail(p, "trailing comma is not allowed", comma); break; }
            continue;
        }
        if (*c == '}') { c++; goto defaults; }
        fail(p, "unexpected character, expected ',' or '}'", c);
        break;
    }
    Py_DECREF(obj);
    return NULL;
defaults:
    for (int i = 0; i < plan->nfields; i++) {
        if (seen[i / 64] & (uint64_t)1 << (i % 64)) continue;
        const Field *field = &plan->fields[i];
        PyObject *value;
        if (field->default_kind == 1) value = Py_NewRef(field->default_value);
        else if (field->default_kind == 2) value = PyObject_CallNoArgs(field->default_value);
        else if (field->default_kind == 3) continue;
        else {
            Py_DECREF(obj);
            return failf(p, c - 1, "missing required field '%s' of %s", PyUnicode_AsUTF8(field->name), plan->name);
        }
        if (!value) { Py_DECREF(obj); return NULL; }
        int status = set_field(obj, field, value);
        Py_DECREF(value);
        if (status < 0) { Py_DECREF(obj); return NULL; }
    }
    if (plan->post_init) {
        PyObject *result = PyObject_CallMethodNoArgs(obj, post_init_name);
        if (!result) { Py_DECREF(obj); return NULL; }
        Py_DECREF(result);
    }
    p->cur = c;
    p->depth--;
    return obj;
}

static PyObject *parse_typed(Parser *p, const Plan *plan) {
    if (plan->kind == PLAN_ANY) return parse_value(p);
    const unsigned char *c = skip_space(p->cur, p->end);
    p->cur = c;
    if (c >= p->end) return fail(p, MSG_EOF, p->end);
    switch (plan->kind) {
    case PLAN_INT:
    case PLAN_FLOAT: {
        if (*c != '-' && !is_digit(*c)) return type_error(p, plan, c, NULL);
        PyObject *value = parse_number(p);
        if (!value) return NULL;
        if (PyLong_CheckExact(value)) {
            if (plan->kind == PLAN_INT) return value;
            double as_double = PyLong_AsDouble(value);
            Py_DECREF(value);
            if (as_double == -1.0 && PyErr_Occurred()) return NULL;
            return PyFloat_FromDouble(as_double);
        }
        if (plan->kind == PLAN_FLOAT) return value;
        Py_DECREF(value);
        return type_error(p, plan, c, "float");
    }
    case PLAN_STR:
        if (*c != '"') return type_error(p, plan, c, NULL);
        return parse_string(p);
    case PLAN_BOOL:
        if (*c == 't') return parse_literal(p, "true", 4, Py_True);
        if (*c == 'f') return parse_literal(p, "false", 5, Py_False);
        return type_error(p, plan, c, NULL);
    case PLAN_NONE:
        if (*c != 'n') return type_error(p, plan, c, NULL);
        return parse_literal(p, "null", 4, Py_None);
    case PLAN_OPTIONAL:
        if (*c == 'n') return parse_literal(p, "null", 4, Py_None);
        return parse_typed(p, plan->item);
    case PLAN_LIST:
        if (*c != '[') return type_error(p, plan, c, NULL);
        return parse_array(p, plan->item);
    case PLAN_DICT:
        if (*c != '{') return type_error(p, plan, c, NULL);
        return parse_object(p, plan->item);
    default:
        if (*c != '{') return type_error(p, plan, c, NULL);
        return parse_dataclass(p, plan);
    }
}

/* --- entry point ---------------------------------------------------------- */

static void raise_decode_error(const char *message, PyObject *source, const unsigned char *data, Py_ssize_t length, Py_ssize_t byte_offset) {
    if (byte_offset > length) byte_offset = length;
    Py_ssize_t position = 0;
    for (Py_ssize_t i = 0; i < byte_offset; i++) position += (data[i] & 0xC0) != 0x80;  /* bytes to characters */
    PyObject *document = source ? Py_NewRef(source) : PyUnicode_DecodeUTF8((const char *)data, length, "replace");
    if (!document) return;
    PyObject *error = PyObject_CallFunction(decode_error_type, "sOn", message, document, position);
    Py_DECREF(document);
    if (!error) return;
    PyErr_SetObject(decode_error_type, error);
    Py_DECREF(error);
}

static PyObject *loads(PyObject *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    (void)self;
    if (nargs != 1) {
        if (nargs == 0) PyErr_SetString(PyExc_TypeError, "loads() missing 1 required positional argument: 'obj'");
        else PyErr_Format(PyExc_TypeError, "loads() takes exactly 1 positional argument (%zd given)", nargs);
        return NULL;
    }
    PyObject *obj = args[0], *annotation = NULL;
    for (Py_ssize_t i = 0; kwnames && i < PyTuple_GET_SIZE(kwnames); i++) {
        PyObject *name = PyTuple_GET_ITEM(kwnames, i);
        if (PyUnicode_CompareWithASCIIString(name, "type") != 0) {
            PyErr_Format(PyExc_TypeError, "loads() got an unexpected keyword argument '%U'", name);
            return NULL;
        }
        annotation = args[nargs + i];
    }
    const Plan *plan = NULL;
    if (annotation && annotation != Py_None) {
        plan = plan_for(annotation);
        if (!plan) return NULL;
        if (plan->kind == PLAN_ANY) plan = NULL;
    }
    const unsigned char *data;
    Py_ssize_t length;
    Py_buffer view;
    int has_view = 0;
    PyObject *source = NULL;  /* the input when it is already a str */
    if (PyUnicode_CheckExact(obj)) {
        data = (const unsigned char *)PyUnicode_AsUTF8AndSize(obj, &length);
        if (!data) {
            if (!PyErr_ExceptionMatches(PyExc_UnicodeEncodeError)) return NULL;
            PyErr_Clear();
            raise_decode_error(MSG_UTF8, NULL, (const unsigned char *)"", 0, 0);
            return NULL;
        }
        source = obj;
    } else if (PyBytes_CheckExact(obj)) {
        data = (const unsigned char *)PyBytes_AS_STRING(obj);
        length = PyBytes_GET_SIZE(obj);
    } else if (PyByteArray_CheckExact(obj) || PyMemoryView_Check(obj)) {
        if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0) {
            PyErr_Clear();
            raise_decode_error("Input memoryview must be contiguous", NULL, (const unsigned char *)"", 0, 0);
            return NULL;
        }
        has_view = 1;
        data = view.buf;
        length = view.len;
    } else {
        raise_decode_error("Input must be bytes, bytearray, memoryview, or str", NULL, (const unsigned char *)"", 0, 0);
        return NULL;
    }

    Parser p;
    p.start = p.cur = data;
    p.end = data + length;
    p.depth = 0;
    p.error = NULL;
    p.error_at = 0;
    p.stack = p.inline_stack;
    p.stack_len = 0;
    p.stack_cap = INLINE_STACK;
    p.scratch = NULL;
    p.scratch_cap = 0;

    PyObject *result = NULL;
    if (length == 0) {
        fail(&p, "Input is a zero-length, empty document", data);
    } else if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        fail(&p, "UTF-8 byte order mark (BOM) is not supported", data);
    } else {
        result = plan ? parse_typed(&p, plan) : parse_value(&p);
        if (result) {
            const unsigned char *c = skip_space(p.cur, p.end);
            if (c < p.end) {
                Py_CLEAR(result);
                fail(&p, "unexpected content after document", c);
            }
        }
    }
    for (Py_ssize_t i = 0; i < p.stack_len; i++) Py_DECREF(p.stack[i]);
    if (p.stack != p.inline_stack) PyMem_Free(p.stack);
    PyMem_Free(p.scratch);
    if (!result && p.error) raise_decode_error(p.error, source, data, length, p.error_at);
    if (has_view) PyBuffer_Release(&view);
    return result;
}

/* The "--" line gives inspect.signature() a __text_signature__. */
static PyMethodDef loads_method = {"loads", (PyCFunction)(void (*)(void))loads, METH_FASTCALL | METH_KEYWORDS,
    "loads($module, obj, /, *, type=None)\n--\n\nDeserialize JSON to Python objects, or to `type` when given."};

/* Called by yjson_install: adds loads to the module. `support` is the imported
   _yjson_support module, which defines JSONDecodeError. */
int yjson_install_loads(PyObject *module, PyObject *support) {
    memset(hex_values, -1, sizeof hex_values);
    for (int i = 0; i < 16; i++) {
        hex_values[(unsigned char)"0123456789abcdef"[i]] = (signed char)i;
        hex_values[(unsigned char)"0123456789ABCDEF"[i]] = (signed char)i;
    }
    PyObject *error_type = PyObject_GetAttrString(support, "JSONDecodeError");
    if (!error_type) return -1;
    Py_XSETREF(decode_error_type, error_type);
    PyObject *describe = PyObject_GetAttrString(support, "describe_type");
    if (!describe) return -1;
    Py_XSETREF(describe_type, describe);
    if (!plan_cache) plan_cache = PyDict_New();
    if (!post_init_name) post_init_name = PyUnicode_InternFromString("__post_init__");
    if (!plan_cache || !post_init_name) return -1;
    PyObject *module_name = PyUnicode_FromString("yjson");
    if (!module_name) return -1;
    PyObject *func = PyCFunction_NewEx(&loads_method, module, module_name);  /* bound to the module, so inspect drops $module */
    Py_DECREF(module_name);
    if (!func) return -1;
    int status = PyObject_SetAttrString(module, "loads", func);
    Py_DECREF(func);
    return status;
}

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <datetime.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

#if PY_VERSION_HEX < 0x030C0000 || PY_VERSION_HEX >= 0x03100000
#error "mojson supports CPython 3.12 through 3.15"
#endif
#ifdef Py_GIL_DISABLED
#error "mojson reads the default (GIL) object layouts; free-threaded builds are not supported"
#endif
#ifndef MOJSON_TUPLE_ITEMS
#error "build with ./build.sh, which passes the probed object layout to both compilers"
#endif

/* The Mojo loops read these object fields directly. build.sh probes them from
   the target headers (src/layout_probe.c) and passes the same values to both
   compilers; this re-check catches a mismatch between the two. */
#define MOJSON_LAYOUT(name, expr) _Static_assert((expr) == (name), #name " does not match the Python headers")
MOJSON_LAYOUT(MOJSON_PY_MINOR, PY_MINOR_VERSION);
MOJSON_LAYOUT(MOJSON_OB_TYPE, offsetof(PyObject, ob_type));
MOJSON_LAYOUT(MOJSON_OB_SIZE, offsetof(PyVarObject, ob_size));
MOJSON_LAYOUT(MOJSON_TP_NAME, offsetof(PyTypeObject, tp_name));
MOJSON_LAYOUT(MOJSON_FLOAT_VALUE, offsetof(PyFloatObject, ob_fval));
MOJSON_LAYOUT(MOJSON_LONG_TAG, offsetof(PyLongObject, long_value.lv_tag));
MOJSON_LAYOUT(MOJSON_LONG_DIGITS, offsetof(PyLongObject, long_value.ob_digit));
MOJSON_LAYOUT(MOJSON_LIST_ITEMS, offsetof(PyListObject, ob_item));
MOJSON_LAYOUT(MOJSON_TUPLE_ITEMS, offsetof(PyTupleObject, ob_item));
MOJSON_LAYOUT(MOJSON_BYTES_DATA, offsetof(PyBytesObject, ob_sval));
MOJSON_LAYOUT(MOJSON_DICT_USED, offsetof(PyDictObject, ma_used));
MOJSON_LAYOUT(MOJSON_DICT_KEYS, offsetof(PyDictObject, ma_keys));
MOJSON_LAYOUT(MOJSON_STR_LENGTH, offsetof(PyASCIIObject, length));
MOJSON_LAYOUT(MOJSON_STR_STATE, offsetof(PyASCIIObject, state));
MOJSON_LAYOUT(MOJSON_STR_ASCII_DATA, sizeof(PyASCIIObject));
MOJSON_LAYOUT(MOJSON_STR_UTF8_LENGTH, offsetof(PyCompactUnicodeObject, utf8_length));
MOJSON_LAYOUT(MOJSON_STR_UTF8, offsetof(PyCompactUnicodeObject, utf8));
/* Compact integers: lv_tag = ndigits << 3 | sign (0 positive, 1 zero, 2 negative). */
_Static_assert(PyLong_SHIFT == 30, "mojson requires 30-bit CPython integer digits");
_Static_assert(_PyLong_NON_SIZE_BITS == 3 && _PyLong_SIGN_MASK == 3, "mojson requires the 3.12 lv_tag encoding");
_Static_assert(sizeof(digit) == 4, "mojson requires 32-bit integer digits");
/* Py_buffer fields read by the NumPy fast path. */
_Static_assert(offsetof(Py_buffer, len) == 16 && offsetof(Py_buffer, itemsize) == 24 && offsetof(Py_buffer, ndim) == 36
    && offsetof(Py_buffer, format) == 40 && offsetof(Py_buffer, shape) == 48 && offsetof(Py_buffer, strides) == 56
    && sizeof(Py_buffer) <= 128, "Py_buffer layout");

/* All Python ownership and keyword parsing lives here, outside the Mojo loops. */
extern uintptr_t mojson_encode(uintptr_t context, uintptr_t object, uintptr_t request);

static int runtime_failure(const char *what) {
    PyErr_Format(PyExc_ImportError, "mojson was built for CPython %d.%d; this interpreter's %s layout differs",
                 PY_MAJOR_VERSION, PY_MINOR_VERSION, what);
    return 0;
}

#define WORD(obj, offset) (*(uintptr_t *)((char *)(obj) + (offset)))

/* The field positions above are compile-time facts about the headers. This
   confirms them against live objects of the running interpreter, including
   the str state bits and the dict key-table kind byte, which the headers do
   not expose as offsets. Runs once at import. */
int mojson_check_runtime(void) {
    if ((Py_Version >> 16) != (PY_VERSION_HEX >> 16)) {
        PyErr_Format(PyExc_ImportError, "mojson was built for CPython %d.%d, not %lu.%lu",
                     PY_MAJOR_VERSION, PY_MINOR_VERSION, Py_Version >> 24, (Py_Version >> 16) & 0xFF);
        return 0;
    }
    int ok = 0;
    PyObject *ascii = PyUnicode_FromString("mojson"), *wide = PyUnicode_FromString("mojs\xc3\xb6n");
    PyObject *negative = PyLong_FromLong(-5), *zero = PyLong_FromLong(0), *wide_int = PyLong_FromLongLong(1LL << 40);
    PyObject *real = PyFloat_FromDouble(1.5), *list = NULL, *tuple = NULL, *bytes = PyBytes_FromString("xyz");
    PyObject *text_keys = PyDict_New(), *general_keys = PyDict_New();
    if (!ascii || !wide || !negative || !zero || !wide_int || !real || !bytes || !text_keys || !general_keys) goto done;
    list = PyList_New(1);
    tuple = PyTuple_New(1);
    if (!list || !tuple) goto done;
    PyList_SET_ITEM(list, 0, Py_NewRef(real));
    PyTuple_SET_ITEM(tuple, 0, Py_NewRef(real));
    if (PyDict_SetItem(text_keys, ascii, zero) < 0 || PyDict_SetItem(general_keys, zero, zero) < 0) goto done;
    if (WORD(ascii, MOJSON_OB_TYPE) != (uintptr_t)&PyUnicode_Type || *(const char **)((char *)&PyUnicode_Type + MOJSON_TP_NAME) != PyUnicode_Type.tp_name) {
        runtime_failure("object header"); goto done;
    }
    uint32_t state = *(uint32_t *)((char *)ascii + MOJSON_STR_STATE);
    if ((state & 0x60) != 0x60 || (Py_ssize_t)WORD(ascii, MOJSON_STR_LENGTH) != 6
        || (char *)ascii + MOJSON_STR_ASCII_DATA != (char *)PyUnicode_DATA(ascii) || memcmp((char *)ascii + MOJSON_STR_ASCII_DATA, "mojson", 7) != 0) {
        runtime_failure("str"); goto done;
    }
    Py_ssize_t utf8_length = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(wide, &utf8_length);
    state = *(uint32_t *)((char *)wide + MOJSON_STR_STATE);
    if (!utf8 || (state & 0x60) != 0x20 || (Py_ssize_t)WORD(wide, MOJSON_STR_UTF8_LENGTH) != utf8_length || (const char *)WORD(wide, MOJSON_STR_UTF8) != utf8) {
        runtime_failure("compact str utf8 cache"); goto done;
    }
    uintptr_t tag = WORD(negative, MOJSON_LONG_TAG);
    if ((tag & 3) != 2 || (tag >> 3) != 1 || *(uint32_t *)((char *)negative + MOJSON_LONG_DIGITS) != 5
        || (WORD(zero, MOJSON_LONG_TAG) & 3) != 1 || (WORD(wide_int, MOJSON_LONG_TAG) >> 3) != 2
        || ((uint64_t)((uint32_t *)((char *)wide_int + MOJSON_LONG_DIGITS))[1] << 30) != (1ULL << 40)) {
        runtime_failure("int"); goto done;
    }
    double value;
    memcpy(&value, (char *)real + MOJSON_FLOAT_VALUE, sizeof value);
    if (value != 1.5) { runtime_failure("float"); goto done; }
    if ((Py_ssize_t)WORD(list, MOJSON_OB_SIZE) != 1 || ((PyObject **)WORD(list, MOJSON_LIST_ITEMS))[0] != real) {
        runtime_failure("list"); goto done;
    }
    if ((Py_ssize_t)WORD(tuple, MOJSON_OB_SIZE) != 1 || (PyObject *)WORD(tuple, MOJSON_TUPLE_ITEMS) != real) {
        runtime_failure("tuple"); goto done;
    }
    if ((Py_ssize_t)WORD(bytes, MOJSON_OB_SIZE) != 3 || memcmp((char *)bytes + MOJSON_BYTES_DATA, "xyz", 4) != 0) {
        runtime_failure("bytes"); goto done;
    }
    /* dk_kind: DICT_KEYS_GENERAL (0) only when a key is not str. */
    if ((Py_ssize_t)WORD(text_keys, MOJSON_DICT_USED) != 1 || ((unsigned char *)WORD(text_keys, MOJSON_DICT_KEYS))[10] == 0
        || ((unsigned char *)WORD(general_keys, MOJSON_DICT_KEYS))[10] != 0) {
        runtime_failure("dict"); goto done;
    }
    ok = 1;
done:
    Py_XDECREF(ascii); Py_XDECREF(wide); Py_XDECREF(negative); Py_XDECREF(zero); Py_XDECREF(wide_int);
    Py_XDECREF(real); Py_XDECREF(list); Py_XDECREF(tuple); Py_XDECREF(bytes); Py_XDECREF(text_keys); Py_XDECREF(general_keys);
    return ok;
}

PyObject *mojson_null(void) { return NULL; }

typedef struct {
    uintptr_t context;
    PyObject *convert, *key_string, *fragment_type, *dataclass_fields_type, *uuid_type;
    PyObject *dataclass_name, *field_kind_name, *field_sentinel;
    PyObject *enum_type;
} ModuleState;

typedef struct {
    ModuleState *state;
    PyObject *capsule, *default_fn;
    long option;
    int conversions;
    int single_ancestor;
    PyObject *keepalive;
} Request;

static PyObject *dumps(PyObject *, PyObject *const *, Py_ssize_t, PyObject *);
static PyObject *dumps_socket(PyObject *, PyObject *const *, Py_ssize_t, PyObject *);
static PyObject *socket_finish(PyObject *);
static PyMethodDef dumps_method = {
    "dumps", (PyCFunction)(void (*)(void))dumps, METH_FASTCALL | METH_KEYWORDS,
    "dumps(obj, /, default=None, option=None) -> bytes"
};
static PyMethodDef socket_method = {
    "dumps_socket", (PyCFunction)(void (*)(void))dumps_socket, METH_FASTCALL | METH_KEYWORDS,
    "dumps_socket(obj, /, default=None) -> bytes"
};

static void destroy_state(PyObject *capsule) {
    ModuleState *state = PyCapsule_GetPointer(capsule, "mojson.state");
    if (!state) { PyErr_Clear(); return; }
    Py_XDECREF(state->convert);
    Py_XDECREF(state->key_string);
    Py_XDECREF(state->fragment_type);
    Py_XDECREF(state->dataclass_fields_type);
    Py_XDECREF(state->uuid_type);
    Py_XDECREF(state->dataclass_name);
    Py_XDECREF(state->field_kind_name);
    Py_XDECREF(state->field_sentinel);
    Py_XDECREF(state->enum_type);
    PyMem_Free(state);
}

static void wrap_error(int callback) {
    if (!PyErr_Occurred()) {
        PyErr_SetString(PyExc_TypeError, "Unable to serialize object");
        return;
    }
    if ((!callback && (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_MemoryError)))
        || PyErr_ExceptionMatches(PyExc_KeyboardInterrupt) || PyErr_ExceptionMatches(PyExc_SystemExit)) return;
    PyObject *value = PyErr_GetRaisedException();
    PyObject *message = PyObject_Str(value);
    PyObject *error = message ? PyObject_CallOneArg(PyExc_TypeError, message) : NULL;
    Py_XDECREF(message);
    if (error) {
        PyException_SetCause(error, value);  /* steals value */
        PyErr_SetObject(PyExc_TypeError, error);
        Py_DECREF(error);
    } else {
        Py_DECREF(value);
    }
}

static inline __attribute__((always_inline)) PyObject *dumps_impl(PyObject *capsule, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames, int socket) {
    if (nargs < 1 || nargs > 3) {
        PyErr_SetString(PyExc_TypeError, "dumps() requires one object and at most three positional arguments");
        return NULL;
    }
    ModuleState *state = PyCapsule_GetPointer(capsule, "mojson.state");
    if (!state) return NULL;
    PyObject *default_fn = nargs >= 2 ? args[1] : Py_None;
    PyObject *option_obj = nargs >= 3 ? args[2] : Py_None;
    int default_seen = nargs >= 2, option_seen = nargs >= 3;
    if (kwnames) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(kwnames); i++) {
            PyObject *name = PyTuple_GET_ITEM(kwnames, i);
            if (PyUnicode_CompareWithASCIIString(name, "default") == 0 && !default_seen) {
                default_fn = args[nargs + i]; default_seen = 1;
            } else if (PyUnicode_CompareWithASCIIString(name, "option") == 0 && !option_seen) {
                option_obj = args[nargs + i]; option_seen = 1;
            } else {
                PyErr_Format(PyExc_TypeError, "Unexpected or duplicate keyword: %U", name);
                return NULL;
            }
        }
    }
    long option = 0;
    if (option_obj != Py_None) {
        if (!PyLong_CheckExact(option_obj) || (option = PyLong_AsLong(option_obj)) < 0 || option > 4095) {
            PyErr_Clear();
            PyErr_SetString(PyExc_TypeError, "Invalid opts");
            return NULL;
        }
    }
    if (socket) option |= 65536 | 4 | 512 | 2048;
    Request request = {state, capsule, default_fn, option, 0, 0, NULL};
    PyObject *result = (PyObject *)mojson_encode(state->context, (uintptr_t)args[0], (uintptr_t)&request);
    Py_XDECREF(request.keepalive);
    if (!result) wrap_error(0);
    return socket && result ? socket_finish(result) : result;
}

static PyObject *dumps(PyObject *capsule, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    return dumps_impl(capsule, args, nargs, kwnames, 0);
}

static PyObject *dumps_socket(PyObject *capsule, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    if (nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "dumps_socket() accepts an object and default callback");
        return NULL;
    }
    if (kwnames) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(kwnames); i++) {
            if (PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(kwnames, i), "default") != 0) {
                PyErr_SetString(PyExc_TypeError, "dumps_socket() only accepts default=");
                return NULL;
            }
        }
    }
    return dumps_impl(capsule, args, nargs, kwnames, 1);
}

/* Decimal conversion only for the cold, arbitrary-size integer path. CPython's
   formatter handles digit limits and switches algorithms for very large values. */
uintptr_t mojson_big_integer(uintptr_t object) {
    PyObject *obj = (PyObject *)object;
    Py_INCREF(obj);
    PyObject *result = PyLong_Type.tp_str(obj);
    Py_DECREF(obj);
    return (uintptr_t)result;
}

/* Preserve lone surrogates as JSON escapes, without invoking a JSON encoder. */
uintptr_t mojson_surrogate_string(uintptr_t object) {
    if (!PyErr_ExceptionMatches(PyExc_UnicodeEncodeError)) return 0;
    PyErr_Clear();
    PyObject *obj = (PyObject *)object;
    Py_ssize_t size = PyUnicode_GET_LENGTH(obj);
    if (size > (PY_SSIZE_T_MAX - 2) / 6) return (uintptr_t)PyErr_NoMemory();
    PyObject *out = PyBytes_FromStringAndSize(NULL, size * 6 + 2);
    if (!out) return 0;
    char *dst = PyBytes_AS_STRING(out);
    Py_ssize_t n = 0;
    int kind = PyUnicode_KIND(obj);
    void *data = PyUnicode_DATA(obj);
    static const char hex[] = "0123456789abcdef";
    dst[n++] = '"';
    for (Py_ssize_t i = 0; i < size; i++) {
        Py_UCS4 c = PyUnicode_READ(kind, data, i);
        if (c < 32 || (c >= 0xd800 && c <= 0xdfff)) {
            const char *short_escape = c == 8 ? "\\b" : c == 9 ? "\\t" : c == 10 ? "\\n" : c == 12 ? "\\f" : c == 13 ? "\\r" : NULL;
            if (short_escape) { dst[n++] = short_escape[0]; dst[n++] = short_escape[1]; }
            else {
                dst[n++] = '\\'; dst[n++] = 'u';
                dst[n++] = hex[(c >> 12) & 15]; dst[n++] = hex[(c >> 8) & 15];
                dst[n++] = hex[(c >> 4) & 15]; dst[n++] = hex[c & 15];
            }
        } else if (c == '"' || c == '\\') { dst[n++] = '\\'; dst[n++] = (char)c; }
        else if (c < 0x80) dst[n++] = (char)c;
        else if (c < 0x800) { dst[n++] = (char)(0xc0 | (c >> 6)); dst[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) {
            dst[n++] = (char)(0xe0 | (c >> 12)); dst[n++] = (char)(0x80 | ((c >> 6) & 63)); dst[n++] = (char)(0x80 | (c & 63));
        } else {
            dst[n++] = (char)(0xf0 | (c >> 18)); dst[n++] = (char)(0x80 | ((c >> 12) & 63));
            dst[n++] = (char)(0x80 | ((c >> 6) & 63)); dst[n++] = (char)(0x80 | (c & 63));
        }
    }
    dst[n++] = '"';
    if (_PyBytes_Resize(&out, n) < 0) return 0;
    return (uintptr_t)out;
}

/* Scan emitted bytes, never walk/re-serialize Python containers. The usual
   payload returns the same bytes object. Only marker-bearing packets copy. */
static PyObject *socket_finish(PyObject *out) {
    const char *src = PyBytes_AS_STRING(out);
    Py_ssize_t size = PyBytes_GET_SIZE(out);
    int markers = 0;
    for (Py_ssize_t i = 0; i + 9 <= size;) {
        const char *p = memchr(src + i, '_', (size_t)(size - i - 8));
        if (!p) break;
        i = p - src;
        if (memcmp(p, "__reflex_", 9) == 0) { markers = 1; break; }
        i++;
    }
    if (!markers) return out;
    if (size > (PY_SSIZE_T_MAX - 2) / 6) { Py_DECREF(out); return PyErr_NoMemory(); }
    PyObject *rewritten = PyBytes_FromStringAndSize(NULL, size * 6 + 2);
    if (!rewritten) { Py_DECREF(out); return NULL; }
    char *dst = PyBytes_AS_STRING(rewritten);
    Py_ssize_t n = 0, i = 0;
    while (i < size) {
        if (src[i] == '"') {
            Py_ssize_t start = i++, text = i;
            while (i < size && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < size) i++;
                i++;
            }
            Py_ssize_t len = i - text;
            if (i < size) i++;
            Py_ssize_t after = i;
            while (after < size && (src[after] == ' ' || src[after] == '\n' || src[after] == '\t' || src[after] == '\r')) after++;
            int key = after < size && src[after] == ':';
            int collision = !key && ((len == 14 && (memcmp(src + text, "__reflex_nan__", 14) == 0 || memcmp(src + text, "__reflex_inf__", 14) == 0))
                || (len == 18 && memcmp(src + text, "__reflex_neg_inf__", 18) == 0)
                || (len >= 14 && memcmp(src + text, "__reflex_esc__", 14) == 0));
            if (collision) {
                dst[n++] = '"'; memcpy(dst + n, "__reflex_esc__", 14); n += 14;
                memcpy(dst + n, src + text, (size_t)(i - text)); n += i - text;
            } else { memcpy(dst + n, src + start, (size_t)(i - start)); n += i - start; }
        } else {
            const char *sentinel = NULL;
            Py_ssize_t consumed = 0;
            if (size - i >= 3 && memcmp(src + i, "NaN", 3) == 0) { sentinel = "\"__reflex_nan__\""; consumed = 3; }
            else if (size - i >= 8 && memcmp(src + i, "Infinity", 8) == 0) { sentinel = "\"__reflex_inf__\""; consumed = 8; }
            else if (size - i >= 9 && memcmp(src + i, "-Infinity", 9) == 0) { sentinel = "\"__reflex_neg_inf__\""; consumed = 9; }
            if (sentinel) { size_t len = strlen(sentinel); memcpy(dst + n, sentinel, len); n += (Py_ssize_t)len; i += consumed; }
            else dst[n++] = src[i++];
        }
    }
    Py_DECREF(out);
    if (_PyBytes_Resize(&rewritten, n) < 0) return NULL;
    return rewritten;
}

long mojson_options(uintptr_t request) { return ((Request *)request)->option; }

int mojson_enter_fallback(uintptr_t request_ptr) {
    Request *request = (Request *)request_ptr;
    if (request->conversions >= 254) {
        PyErr_SetString(PyExc_TypeError, "default serializer recursion limit exceeded");
        return 0;
    }
    request->conversions++;
    return 1;
}

void mojson_leave_fallback(uintptr_t request) { ((Request *)request)->conversions--; }

static int retain_ancestors(Request *request, uintptr_t ancestors_ptr, long depth) {
    PyObject **ancestors = (PyObject **)ancestors_ptr;
    if (depth == 0) return 1;
    if (depth == 1 && !request->keepalive) {
        request->keepalive = Py_NewRef(ancestors[1]);
        request->single_ancestor = 1;
        return 1;
    }
    if (request->single_ancestor) {
        if (depth == 1 && request->keepalive == ancestors[1]) return 1;
        for (long i = 1; i <= depth; i++) Py_INCREF(ancestors[i]);
        PyObject *expanded = PyList_New(depth + 1);
        if (!expanded) {
            for (long i = 1; i <= depth; i++) Py_DECREF(ancestors[i]);
            return 0;
        }
        /* Transfer the existing and newly pinned references into the list. */
        PyList_SET_ITEM(expanded, 0, request->keepalive);
        for (long i = 1; i <= depth; i++) PyList_SET_ITEM(expanded, i, ancestors[i]);
        request->keepalive = expanded;
        request->single_ancestor = 0;
        return 1;
    }
    if (!request->keepalive) {
        /* Creating the list can run GC and arbitrary finalizers. Pin first. */
        for (long i = 1; i <= depth; i++) Py_INCREF(ancestors[i]);
        request->keepalive = PyList_New(0);
        int ok = request->keepalive != NULL;
        for (long i = 1; i <= depth; i++) {
            if (ok && PyList_Append(request->keepalive, ancestors[i]) < 0) ok = 0;
        }
        for (long i = 1; i <= depth; i++) Py_DECREF(ancestors[i]);
        return ok;
    }
    for (long i = 1; i <= depth; i++) {
        if (PyList_Append(request->keepalive, ancestors[i]) < 0) return 0;
    }
    return 1;
}

int mojson_pin_ancestors(uintptr_t request, uintptr_t ancestors, long depth) {
    return retain_ancestors((Request *)request, ancestors, depth);
}

static void two_digits(char *dst, int value) {
    dst[0] = (char)('0' + value / 10);
    dst[1] = (char)('0' + value % 10);
}

/* Exact stdlib types have no user callbacks; custom tzinfo stays in the cold helper. */
static PyObject *datetime_string(PyObject *obj, long option, int *handled) {
    int is_datetime = PyDateTime_CheckExact(obj), is_time = PyTime_CheckExact(obj);
    *handled = is_datetime || is_time || PyDate_CheckExact(obj);
    if (!*handled) return NULL;
    PyObject *tzinfo = is_datetime ? PyDateTime_DATE_GET_TZINFO(obj) : (is_time ? PyDateTime_TIME_GET_TZINFO(obj) : Py_None);
    if (is_time && tzinfo != Py_None) {
        PyErr_SetString(PyExc_TypeError, "datetime.time must not have tzinfo");
        return NULL;
    }
    if (tzinfo != Py_None && !Py_IS_TYPE(tzinfo, Py_TYPE(PyDateTimeAPI->TimeZone_UTC))) {
        *handled = 0;
        return NULL;
    }
    char text[40];
    int size = 0;
    if (!is_time) {
        int year = PyDateTime_GET_YEAR(obj);
        two_digits(text, year / 100); two_digits(text + 2, year % 100);
        text[4] = '-'; two_digits(text + 5, PyDateTime_GET_MONTH(obj));
        text[7] = '-'; two_digits(text + 8, PyDateTime_GET_DAY(obj));
        size = 10;
        if (is_datetime) text[size++] = 'T';
    }
    if (is_datetime || is_time) {
        int hour = is_time ? PyDateTime_TIME_GET_HOUR(obj) : PyDateTime_DATE_GET_HOUR(obj);
        int minute = is_time ? PyDateTime_TIME_GET_MINUTE(obj) : PyDateTime_DATE_GET_MINUTE(obj);
        int second = is_time ? PyDateTime_TIME_GET_SECOND(obj) : PyDateTime_DATE_GET_SECOND(obj);
        int microsecond = is_time ? PyDateTime_TIME_GET_MICROSECOND(obj) : PyDateTime_DATE_GET_MICROSECOND(obj);
        two_digits(text + size, hour); text[size + 2] = ':';
        two_digits(text + size + 3, minute); text[size + 5] = ':';
        two_digits(text + size + 6, second); size += 8;
        if (microsecond && !(option & 8)) {
            text[size++] = '.';
            for (int i = 5; i >= 0; i--) { text[size + i] = (char)('0' + microsecond % 10); microsecond /= 10; }
            size += 6;
        }
    }
    if (is_datetime && (tzinfo != Py_None || (option & 2))) {
        long seconds = 0;
        int offset_microseconds = 0;
        if (tzinfo != Py_None) {
            PyObject *offset = PyObject_CallMethod(obj, "utcoffset", NULL);
            if (!offset) return NULL;
            seconds = (long)PyDateTime_DELTA_GET_DAYS(offset) * 86400 + PyDateTime_DELTA_GET_SECONDS(offset);
            offset_microseconds = PyDateTime_DELTA_GET_MICROSECONDS(offset);
            Py_DECREF(offset);
        }
        if (seconds == 0 && offset_microseconds == 0 && (option & 128)) {
            text[size++] = 'Z';
        } else {
            text[size++] = seconds < 0 ? '-' : '+';
            if (seconds < 0) seconds = -seconds;
            int minutes = (int)((seconds + 30) / 60);
            two_digits(text + size, minutes / 60); text[size + 2] = ':';
            two_digits(text + size + 3, minutes % 60); size += 5;
        }
    }
    return PyUnicode_FromStringAndSize(text, size);
}

static PyObject *uuid_string(PyObject *obj) {
    PyObject *integer = PyObject_GetAttrString(obj, "int");
    if (!integer) return NULL;
    unsigned char bytes[16];
    if (!PyLong_CheckExact(integer)) {
        Py_DECREF(integer);
        PyErr_SetString(PyExc_TypeError, "UUID.int must be an integer");
        return NULL;
    }
#if PY_VERSION_HEX >= 0x030D0000
    Py_ssize_t needed = PyLong_AsNativeBytes(integer, bytes, 16, Py_ASNATIVEBYTES_BIG_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER);
    Py_DECREF(integer);
    if (needed < 0) return NULL;
    if (needed > 16) {
        PyErr_SetString(PyExc_TypeError, "UUID.int exceeds 128 bits");
        return NULL;
    }
#else
    int status = _PyLong_AsByteArray((PyLongObject *)integer, bytes, 16, 0, 0);
    Py_DECREF(integer);
    if (status < 0) return NULL;
#endif
    static const char hex[] = "0123456789abcdef";
    char text[36];
    int size = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text[size++] = '-';
        text[size++] = hex[bytes[i] >> 4]; text[size++] = hex[bytes[i] & 15];
    }
    return PyUnicode_FromStringAndSize(text, 36);
}

static PyObject *dataclass_fields(ModuleState *state, PyObject *obj, PyObject *schema) {
    PyObject *items = PyDict_Items(schema), *result = PyDict_New();
    if (!items || !result) { Py_XDECREF(items); Py_XDECREF(result); return NULL; }
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items); i++) {
        PyObject *pair = PyList_GET_ITEM(items, i);
        PyObject *name = PyTuple_GET_ITEM(pair, 0), *field = PyTuple_GET_ITEM(pair, 1);
        if (!PyUnicode_Check(name)) {
            PyErr_SetString(PyExc_TypeError, "Dataclass field name must be str");
            goto fail;
        }
        if (PyUnicode_GET_LENGTH(name) && PyUnicode_ReadChar(name, 0) == '_') continue;
        PyObject *kind = PyObject_GetAttr(field, state->field_kind_name);
        if (!kind) goto fail;
        int serialize = kind == state->field_sentinel;
        Py_DECREF(kind);
        if (!serialize) continue;
        PyObject *value = PyObject_GetAttr(obj, name);
        if (!value) goto fail;
        int status = PyDict_SetItem(result, name, value);
        Py_DECREF(value);
        if (status < 0) goto fail;
    }
    Py_DECREF(items);
    return result;
fail:
    Py_DECREF(items); Py_DECREF(result);
    return NULL;
}

static uintptr_t convert_object(uintptr_t request_ptr, uintptr_t object, int *is_fragment, uintptr_t ancestors_ptr, long depth) {
    Request *request = (Request *)request_ptr;
    PyObject *obj = (PyObject *)object;
    if (Py_IS_TYPE(obj, (PyTypeObject *)request->state->fragment_type)) {
        *is_fragment = 1;
        PyObject *data = PyObject_GetAttrString(obj, "_data");
        if (data && !PyBytes_CheckExact(data)) {
            Py_DECREF(data);
            PyErr_SetString(PyExc_TypeError, "Fragment requires bytes data");
            return 0;
        }
        return (uintptr_t)data;
    }
    *is_fragment = 0;
    if (!(request->option & 512)) {
        int handled;
        PyObject *result = datetime_string(obj, request->option, &handled);
        if (handled) return (uintptr_t)result;
    }
    if (!(request->option & 65536) && Py_IS_TYPE(obj, (PyTypeObject *)request->state->uuid_type)) return (uintptr_t)uuid_string(obj);
    /* Only cold Python callbacks need ownership of the active borrowed containers. */
    if (!retain_ancestors(request, ancestors_ptr, depth)) return 0;
    if ((request->option & 65536) && request->default_fn != Py_None
        && !PyLong_Check(obj) && !PyUnicode_Check(obj)
        && (PyObject_TypeCheck(obj, (PyTypeObject *)request->state->enum_type)
            || PyObject_TypeCheck(obj, (PyTypeObject *)request->state->uuid_type))) {
        PyObject *result = PyObject_CallOneArg(request->default_fn, obj);
        if (!result && PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1);
        return (uintptr_t)result;
    }
    if (!(request->option & 2048) && !PyType_Check(obj)) {
        PyObject *schema = _PyType_Lookup(Py_TYPE(obj), request->state->dataclass_name);
        if (schema && PyDict_Check(schema)) {
            *is_fragment = 2;
            return (uintptr_t)dataclass_fields(request->state, obj, schema);
        }
    }
    if (PyObject_TypeCheck(obj, (PyTypeObject *)request->state->enum_type)) {
        return (uintptr_t)PyObject_GetAttrString(obj, "value");
    }
    if (!(request->option & 256)) {
        if (PyUnicode_Check(obj)) return (uintptr_t)PyUnicode_FromObject(obj);
        if (PyLong_Check(obj)) return (uintptr_t)_PyLong_Copy((PyLongObject *)obj);
        if (PyList_Check(obj)) return (uintptr_t)PyList_GetSlice(obj, 0, PyList_GET_SIZE(obj));
        if (PyDict_Check(obj)) return (uintptr_t)PyDict_Copy(obj);
    }
    if (strncmp(Py_TYPE(obj)->tp_name, "numpy.", 6) == 0) {
        return (uintptr_t)PyObject_CallMethod(obj, "tolist", NULL);
    }
    if (PyObject_TypeCheck(obj, (PyTypeObject *)request->state->uuid_type)) return (uintptr_t)PyObject_Str(obj);
    int special_datetime = !(request->option & 512) && (PyDate_Check(obj) || PyTime_Check(obj));
    PyObject *schema = !(request->option & 2048) && !PyType_Check(obj)
        ? _PyType_Lookup(Py_TYPE(obj), request->state->dataclass_name) : NULL;
    if (!special_datetime && !schema) {
        if (request->default_fn == Py_None) {
            PyErr_Format(PyExc_TypeError, "Type is not JSON serializable: %s", Py_TYPE(obj)->tp_name);
            return 0;
        }
        PyObject *result = PyObject_CallOneArg(request->default_fn, obj);
        if (!result && PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1);
        return (uintptr_t)result;
    }
    PyObject *option = PyLong_FromLong(request->option);
    if (!option) return 0;
    PyObject *result = PyObject_CallFunctionObjArgs(request->state->convert, obj, request->default_fn, option, NULL);
    Py_DECREF(option);
    if (result && Py_IS_TYPE(result, (PyTypeObject *)request->state->dataclass_fields_type)) {
        PyObject *fields = PyDict_Copy(result);
        Py_DECREF(result);
        *is_fragment = 2;
        return (uintptr_t)fields;
    }
    return (uintptr_t)result;
}

uintptr_t mojson_convert(uintptr_t request, uintptr_t object, int *kind, uintptr_t ancestors, long depth) {
    /* Attribute getters may remove the last container reference to this object. */
    Py_INCREF((PyObject *)object);
    uintptr_t result = convert_object(request, object, kind, ancestors, depth);
    Py_DECREF((PyObject *)object);
    return result;
}

static PyObject *key_string(Request *request, PyObject *key) {
    if (PyUnicode_Check(key)) return Py_NewRef(key);
    if (!(request->option & 4)) {
        PyErr_SetString(PyExc_TypeError, "Dict key must be str");
        return NULL;
    }
    if (key == Py_None) return PyUnicode_FromString("null");
    if (key == Py_True) return PyUnicode_FromString("true");
    if (key == Py_False) return PyUnicode_FromString("false");
    if (PyLong_CheckExact(key)) {
        if (request->option & 65536) return PyLong_Type.tp_str(key);
        int overflow = 0;
        PyLong_AsLongLongAndOverflow(key, &overflow);
        if (overflow < 0 || (overflow > 0 && PyLong_AsUnsignedLongLong(key) == (unsigned long long)-1 && PyErr_Occurred())) {
            PyErr_Clear(); PyErr_SetString(PyExc_TypeError, "Integer exceeds 64-bit range"); return NULL;
        }
        return PyObject_Str(key);
    }
    if (PyFloat_CheckExact(key)) {
        if ((request->option & 65536) && !isfinite(PyFloat_AS_DOUBLE(key))) {
            double value = PyFloat_AS_DOUBLE(key);
            return PyUnicode_FromString(isnan(value) ? "NaN" : value > 0 ? "Infinity" : "-Infinity");
        }
        if (!isfinite(PyFloat_AS_DOUBLE(key))) return PyUnicode_FromString("null");
        Request scalar = {request->state, request->capsule, Py_None, 0, 0, 0, NULL};
        PyObject *bytes = (PyObject *)mojson_encode(request->state->context, (uintptr_t)key, (uintptr_t)&scalar);
        if (!bytes) return NULL;
        PyObject *text = PyUnicode_DecodeUTF8(PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes), "strict");
        Py_DECREF(bytes);
        return text;
    }
    int handled;
    PyObject *text = datetime_string(key, request->option, &handled);
    if (handled) return text;
    if (Py_IS_TYPE(key, (PyTypeObject *)request->state->uuid_type)) return uuid_string(key);
    PyObject *option = PyLong_FromLong(request->option);
    PyObject *native = PyCFunction_New(&dumps_method, request->capsule);
    if (!option || !native) { Py_XDECREF(option); Py_XDECREF(native); return NULL; }
    PyObject *result = PyObject_CallFunctionObjArgs(request->state->key_string, key, option, native, NULL);
    Py_DECREF(option); Py_DECREF(native);
    return result;
}

static int compare_pairs(const void *lhs, const void *rhs) {
    PyObject *a = *(PyObject *const *)lhs, *b = *(PyObject *const *)rhs;
    return PyUnicode_Compare(PyTuple_GET_ITEM(a, 0), PyTuple_GET_ITEM(b, 0));
}

typedef struct {
    PyObject *key, *value;
    const char *utf8;
    Py_ssize_t length;
} SortedItem;

_Static_assert(sizeof(SortedItem) == 4 * sizeof(uintptr_t), "SortedItem ABI");

static int compare_sorted_items(const void *left, const void *right) {
    const SortedItem *a = left, *b = right;
    Py_ssize_t length = a->length < b->length ? a->length : b->length;
    int order = memcmp(a->utf8, b->utf8, (size_t)length);
    if (order) return order;
    return (a->length > b->length) - (a->length < b->length);
}

void mojson_release_sorted(uintptr_t items_ptr, long count, uintptr_t storage) {
    SortedItem *items = (SortedItem *)items_ptr;
    for (long i = 0; i < count; i++) {
        Py_DECREF(items[i].key);
        Py_DECREF(items[i].value);
    }
    if (items_ptr != storage) PyMem_Free(items);
}

uintptr_t mojson_sort_unicode(uintptr_t object, uintptr_t storage, long capacity) {
    PyObject *dict = (PyObject *)object;
    Py_ssize_t count = PyDict_GET_SIZE(dict), position = 0, filled = 0;
    SortedItem *items = count <= capacity ? (SortedItem *)storage
        : PyMem_Malloc((size_t)count * sizeof(SortedItem));
    if (!items) { PyErr_NoMemory(); return 0; }
    PyObject *key, *value;
    while (PyDict_Next(dict, &position, &key, &value)) {
        Py_ssize_t length;
        const char *utf8 = PyUnicode_AsUTF8AndSize(key, &length);
        if (!utf8) {
            mojson_release_sorted((uintptr_t)items, filled, storage);
            return 0;
        }
        /* Callbacks may mutate the source dict after collection. */
        items[filled++] = (SortedItem){Py_NewRef(key), Py_NewRef(value), utf8, length};
    }
    if (count <= 16) {
        for (Py_ssize_t i = 1; i < count; i++) {
            SortedItem item = items[i];
            Py_ssize_t j = i;
            while (j && compare_sorted_items(items + j - 1, &item) > 0) {
                items[j] = items[j - 1];
                j--;
            }
            items[j] = item;
        }
    } else {
        qsort(items, (size_t)count, sizeof(SortedItem), compare_sorted_items);
    }
    return (uintptr_t)items;
}

uintptr_t mojson_prepare_items(uintptr_t request_ptr, uintptr_t object, int skip_sort, uintptr_t ancestors, long depth) {
    Request *request = (Request *)request_ptr;
    if (!retain_ancestors(request, ancestors, depth)) return 0;
    PyObject *items = PyDict_Items((PyObject *)object);
    if (!items) return 0;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items); i++) {
        PyObject *pair = PyList_GET_ITEM(items, i), *key = PyTuple_GET_ITEM(pair, 0);
        if (PyUnicode_Check(key)) continue;
        PyObject *text = key_string(request, key);
        if (!text) { Py_DECREF(items); return 0; }
        PyObject *replacement = PyTuple_Pack(2, text, PyTuple_GET_ITEM(pair, 1));
        Py_DECREF(text);
        if (!replacement) { Py_DECREF(items); return 0; }
        PyList_SET_ITEM(items, i, replacement);
        Py_DECREF(pair);
    }
    if ((request->option & 32) && !skip_sort && PyList_GET_SIZE(items) > 1) {
        qsort(((PyListObject *)items)->ob_item, (size_t)PyList_GET_SIZE(items), sizeof(PyObject *), compare_pairs);
    }
    return (uintptr_t)items;
}

int mojson_strict_integer(uintptr_t obj) {
    int overflow = 0;
    long long value = PyLong_AsLongLongAndOverflow((PyObject *)obj, &overflow);
    if (overflow || value < -9007199254740991LL || value > 9007199254740991LL) {
        PyErr_SetString(PyExc_TypeError, "Integer exceeds 53-bit range");
        return 0;
    }
    return !PyErr_Occurred();
}

void mojson_error(int code) {
    if (PyErr_Occurred()) return;
    const char *message = "Integer exceeds 64-bit range or invalid UTF-8 string";
    if (code == 3) message = "Recursion limit exceeded";
    if (code == 4) message = "Dict key must be str";
    if (code == 5) message = "Unsupported type";
    if (code == 8) message = "Dictionary changed during serialization";
    PyErr_SetString(PyExc_TypeError, message);
}

int mojson_install(uintptr_t module_ptr, uintptr_t context) {
    PyObject *module = (PyObject *)module_ptr;
    PyDateTime_IMPORT;
    if (!PyDateTimeAPI) return -1;
    PyObject *support = PyImport_ImportModule("_mojson_support");
    if (!support) return -1;
    ModuleState *state = PyMem_Calloc(1, sizeof(ModuleState));
    if (!state) { Py_DECREF(support); PyErr_NoMemory(); return -1; }
    state->context = context;
    state->convert = PyObject_GetAttrString(support, "convert");
    state->key_string = PyObject_GetAttrString(support, "_key_string");
    state->fragment_type = PyObject_GetAttrString(support, "Fragment");
    state->dataclass_fields_type = PyObject_GetAttrString(support, "_DataclassFields");
    PyObject *uuid_module = PyObject_GetAttrString(support, "uuid");
    if (uuid_module) {
        state->uuid_type = PyObject_GetAttrString(uuid_module, "UUID");
        Py_DECREF(uuid_module);
    }
    state->dataclass_name = PyUnicode_InternFromString("__dataclass_fields__");
    state->field_kind_name = PyUnicode_InternFromString("_field_type");
    PyObject *dataclasses = PyObject_GetAttrString(support, "dataclasses");
    if (dataclasses) {
        state->field_sentinel = PyObject_GetAttrString(dataclasses, "_FIELD");
        Py_DECREF(dataclasses);
    }
    PyObject *enum_module = PyObject_GetAttrString(support, "enum");
    if (enum_module) {
        state->enum_type = PyObject_GetAttrString(enum_module, "Enum");
        Py_DECREF(enum_module);
    }
    PyObject *capsule = PyCapsule_New(state, "mojson.state", destroy_state);
    if (!capsule) {
        Py_XDECREF(state->convert); Py_XDECREF(state->key_string); Py_XDECREF(state->fragment_type);
        Py_XDECREF(state->dataclass_fields_type);
        Py_XDECREF(state->uuid_type);
        Py_XDECREF(state->dataclass_name); Py_XDECREF(state->field_kind_name); Py_XDECREF(state->field_sentinel);
        Py_XDECREF(state->enum_type);
        PyMem_Free(state); Py_DECREF(support); return -1;
    }
    if (!state->convert || !state->key_string || !state->fragment_type || !state->dataclass_fields_type || !state->uuid_type
        || !state->dataclass_name || !state->field_kind_name || !state->field_sentinel || !state->enum_type) goto fail;
    PyObject *func = PyCFunction_New(&dumps_method, capsule);
    if (!func) goto fail;
    int status = PyObject_SetAttrString(module, "dumps", func);
    Py_DECREF(func);
    if (status < 0) goto fail;
    func = PyCFunction_New(&socket_method, capsule);
    if (!func) goto fail;
    status = PyObject_SetAttrString(module, "dumps_socket", func);
    Py_DECREF(func);
    if (status < 0) goto fail;
    PyObject *dict = PyModule_GetDict(support), *key, *value;
    Py_ssize_t position = 0;
    while (PyDict_Next(dict, &position, &key, &value)) {
        const char *name = PyUnicode_AsUTF8(key);
        if (!name) goto fail;
        if (strncmp(name, "OPT_", 4) == 0 || strcmp(name, "Fragment") == 0
            || strcmp(name, "JSONEncodeError") == 0 || strcmp(name, "JSONDecodeError") == 0
            || strcmp(name, "loads") == 0) {
            if (PyObject_SetAttr(module, key, value) < 0) goto fail;
        }
    }
    Py_DECREF(capsule); Py_DECREF(support);
    return 0;
fail:
    Py_DECREF(capsule); Py_DECREF(support);
    return -1;
}

#define PY_SSIZE_T_CLEAN
#define Py_BUILD_CORE 1
#include <Python.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "internal/pycore_dict.h"  /* key table layout, re-checked below */
#pragma GCC diagnostic pop
#undef Py_BUILD_CORE
#include <datetime.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <limits.h>

#if PY_VERSION_HEX < 0x030B0000 || PY_VERSION_HEX >= 0x03100000
#error "yjson supports CPython 3.11 through 3.15"
#endif
#ifdef Py_GIL_DISABLED
#error "yjson reads the default (GIL) object layouts; free-threaded builds are not supported"
#endif
#ifndef YJSON_TUPLE_ITEMS
#error "build with ./build.sh, which passes the probed object layout to both compilers"
#endif

/* The Mojo loops read these object fields directly. build.sh probes them from
   the target headers (src/layout_probe.c) and passes the same values to both
   compilers; this re-check catches a mismatch between the two. */
#define YJSON_LAYOUT(name, expr) _Static_assert((expr) == (name), #name " does not match the Python headers")
YJSON_LAYOUT(YJSON_PY_MINOR, PY_MINOR_VERSION);
YJSON_LAYOUT(YJSON_OB_TYPE, offsetof(PyObject, ob_type));
YJSON_LAYOUT(YJSON_OB_SIZE, offsetof(PyVarObject, ob_size));
YJSON_LAYOUT(YJSON_TP_NAME, offsetof(PyTypeObject, tp_name));
YJSON_LAYOUT(YJSON_FLOAT_VALUE, offsetof(PyFloatObject, ob_fval));
#if PY_VERSION_HEX >= 0x030C0000
YJSON_LAYOUT(YJSON_LONG_TAGGED, 1);
YJSON_LAYOUT(YJSON_LONG_TAG, offsetof(PyLongObject, long_value.lv_tag));
YJSON_LAYOUT(YJSON_LONG_DIGITS, offsetof(PyLongObject, long_value.ob_digit));
/* Compact integers: lv_tag = ndigits << 3 | sign (0 positive, 1 zero, 2 negative). */
_Static_assert(_PyLong_NON_SIZE_BITS == 3 && _PyLong_SIGN_MASK == 3, "yjson requires the 3.12 lv_tag encoding");
#else
YJSON_LAYOUT(YJSON_LONG_TAGGED, 0);
YJSON_LAYOUT(YJSON_LONG_TAG, offsetof(PyVarObject, ob_size));
YJSON_LAYOUT(YJSON_LONG_DIGITS, offsetof(PyLongObject, ob_digit));
#endif
YJSON_LAYOUT(YJSON_LIST_ITEMS, offsetof(PyListObject, ob_item));
YJSON_LAYOUT(YJSON_TUPLE_ITEMS, offsetof(PyTupleObject, ob_item));
YJSON_LAYOUT(YJSON_BYTES_DATA, offsetof(PyBytesObject, ob_sval));
YJSON_LAYOUT(YJSON_DICT_USED, offsetof(PyDictObject, ma_used));
YJSON_LAYOUT(YJSON_DICT_KEYS, offsetof(PyDictObject, ma_keys));
YJSON_LAYOUT(YJSON_DICT_VALUES, offsetof(PyDictObject, ma_values));
/* Key table walked directly by the dict serializer (internal/pycore_dict.h). */
YJSON_LAYOUT(YJSON_DK_LOG2_INDEX_BYTES, offsetof(PyDictKeysObject, dk_log2_index_bytes));
YJSON_LAYOUT(YJSON_DK_KIND, offsetof(PyDictKeysObject, dk_kind));
YJSON_LAYOUT(YJSON_DK_NENTRIES, offsetof(PyDictKeysObject, dk_nentries));
YJSON_LAYOUT(YJSON_DK_INDICES, offsetof(PyDictKeysObject, dk_indices));
_Static_assert(DICT_KEYS_GENERAL == 0 && sizeof(PyDictUnicodeEntry) == 16 && offsetof(PyDictUnicodeEntry, me_value) == 8
    && sizeof(PyDictKeyEntry) == 24 && offsetof(PyDictKeyEntry, me_key) == 8 && offsetof(PyDictKeyEntry, me_value) == 16, "dict entry layout");
_Static_assert(sizeof(((PyDictKeysObject *)0)->dk_log2_index_bytes) == 1 && sizeof(((PyDictKeysObject *)0)->dk_kind) == 1, "dict key table header");
YJSON_LAYOUT(YJSON_STR_LENGTH, offsetof(PyASCIIObject, length));
YJSON_LAYOUT(YJSON_STR_STATE, offsetof(PyASCIIObject, state));
YJSON_LAYOUT(YJSON_STR_ASCII_DATA, sizeof(PyASCIIObject));
YJSON_LAYOUT(YJSON_STR_UTF8_LENGTH, offsetof(PyCompactUnicodeObject, utf8_length));
YJSON_LAYOUT(YJSON_STR_UTF8, offsetof(PyCompactUnicodeObject, utf8));
_Static_assert(PyLong_SHIFT == 30, "yjson requires 30-bit CPython integer digits");
_Static_assert(sizeof(digit) == 4, "yjson requires 32-bit integer digits");
/* Py_buffer fields read by the NumPy fast path. */
_Static_assert(offsetof(Py_buffer, len) == 16 && offsetof(Py_buffer, itemsize) == 24 && offsetof(Py_buffer, ndim) == 36
    && offsetof(Py_buffer, format) == 40 && offsetof(Py_buffer, shape) == 48 && offsetof(Py_buffer, strides) == 56
    && sizeof(Py_buffer) <= 128, "Py_buffer layout");

/* All Python ownership and keyword parsing lives here, outside the Mojo loops. */
extern uintptr_t yjson_encode(uintptr_t context, uintptr_t object, uintptr_t request);

#if PY_VERSION_HEX < 0x030C0000
static PyObject *PyErr_GetRaisedException(void) {
    PyObject *type, *value, *traceback;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    if (value && traceback) PyException_SetTraceback(value, traceback);
    Py_XDECREF(type); Py_XDECREF(traceback);
    return value;
}
#endif

static int runtime_failure(const char *what) {
    PyErr_Format(PyExc_ImportError, "yjson was built for CPython %d.%d; this interpreter's %s layout differs",
                 PY_MAJOR_VERSION, PY_MINOR_VERSION, what);
    return 0;
}

#define WORD(obj, offset) (*(uintptr_t *)((char *)(obj) + (offset)))

/* The field positions above are compile-time facts about the headers. This
   confirms them against live objects of the running interpreter, including
   the str state bits and the dict key-table kind byte, which the headers do
   not expose as offsets. Runs once at import. */
/* The Mojo runtime locates libpython by running `python3` from PATH (or
   MOJO_PYTHON) unless MOJO_PYTHON_LIBRARY is set. Inside an extension module
   the interpreter is already loaded, and PATH may hold no python3 or a
   different one, so point it at the object that provides Py_GetVersion:
   libpython3.X.so for shared builds, the executable for static ones. dlopen
   of either returns the image that is already loaded. The path must be
   absolute: for the executable dladdr reports argv[0], which may be relative
   to a directory the program has since left. An empty path also works
   (dlopen("") is the main program). */
static void point_mojo_at_this_interpreter(void) {
    if (getenv("MOJO_PYTHON_LIBRARY")) return;
    Dl_info info;
    char executable[PATH_MAX];
    const char *path = "";
    if (dladdr((void *)&Py_GetVersion, &info) && info.dli_fname && info.dli_fname[0] == '/') {
        path = info.dli_fname;
    } else if (realpath("/proc/self/exe", executable)) {
        path = executable;
    }
    setenv("MOJO_PYTHON_LIBRARY", path, 0);
}

/* Starting the Mojo runtime sets MOJO_PYTHON_LIBRARY, PYTHONEXECUTABLE and
   PYTHONPATH with setenv. Child processes inherit them, and a venv's python
   started with PYTHONEXECUTABLE pointing at the base interpreter loses its
   site-packages. The import snapshots them first and puts them back once the
   runtime is up, on success and failure alike. */
static const char *const runtime_environment[] = {"MOJO_PYTHON_LIBRARY", "PYTHONEXECUTABLE", "PYTHONPATH"};
#define RUNTIME_ENVIRONMENT_SIZE (sizeof(runtime_environment) / sizeof(runtime_environment[0]))
static char *saved_environment[RUNTIME_ENVIRONMENT_SIZE];
static int environment_saved = 0;

static void save_environment(void) {
    for (size_t i = 0; i < RUNTIME_ENVIRONMENT_SIZE; i++) {
        const char *value = getenv(runtime_environment[i]);
        saved_environment[i] = value ? strdup(value) : NULL;
    }
    environment_saved = 1;
}

static void restore_environment(void) {
    if (!environment_saved) return;
    environment_saved = 0;
    for (size_t i = 0; i < RUNTIME_ENVIRONMENT_SIZE; i++) {
        if (saved_environment[i]) {
            setenv(runtime_environment[i], saved_environment[i], 1);
            free(saved_environment[i]);
            saved_environment[i] = NULL;
        } else {
            unsetenv(runtime_environment[i]);
        }
    }
}

int yjson_check_runtime(void) {
    save_environment();
    point_mojo_at_this_interpreter();
    if ((Py_Version >> 16) != (PY_VERSION_HEX >> 16)) {
        PyErr_Format(PyExc_ImportError, "yjson was built for CPython %d.%d, not %lu.%lu",
                     PY_MAJOR_VERSION, PY_MINOR_VERSION, Py_Version >> 24, (Py_Version >> 16) & 0xFF);
        return 0;
    }
    int ok = 0;
    PyObject *ascii = PyUnicode_FromString("yjson"), *wide = PyUnicode_FromString("yjs\xc3\xb6n");
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
    if (WORD(ascii, YJSON_OB_TYPE) != (uintptr_t)&PyUnicode_Type || *(const char **)((char *)&PyUnicode_Type + YJSON_TP_NAME) != PyUnicode_Type.tp_name) {
        runtime_failure("object header"); goto done;
    }
    uint32_t state = *(uint32_t *)((char *)ascii + YJSON_STR_STATE);
    if ((state & 0x60) != 0x60 || (Py_ssize_t)WORD(ascii, YJSON_STR_LENGTH) != 5
        || (char *)ascii + YJSON_STR_ASCII_DATA != (char *)PyUnicode_DATA(ascii) || memcmp((char *)ascii + YJSON_STR_ASCII_DATA, "yjson", 6) != 0) {
        runtime_failure("str"); goto done;
    }
    Py_ssize_t utf8_length = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(wide, &utf8_length);
    state = *(uint32_t *)((char *)wide + YJSON_STR_STATE);
    if (!utf8 || (state & 0x60) != 0x20 || (Py_ssize_t)WORD(wide, YJSON_STR_UTF8_LENGTH) != utf8_length || (const char *)WORD(wide, YJSON_STR_UTF8) != utf8) {
        runtime_failure("compact str utf8 cache"); goto done;
    }
#if YJSON_LONG_TAGGED
    uintptr_t tag = WORD(negative, YJSON_LONG_TAG);
    int int_ok = (tag & 3) == 2 && (tag >> 3) == 1 && (WORD(zero, YJSON_LONG_TAG) & 3) == 1 && (WORD(wide_int, YJSON_LONG_TAG) >> 3) == 2;
#else
    int int_ok = (Py_ssize_t)WORD(negative, YJSON_LONG_TAG) == -1 && (Py_ssize_t)WORD(zero, YJSON_LONG_TAG) == 0
        && (Py_ssize_t)WORD(wide_int, YJSON_LONG_TAG) == 2 && *(uint32_t *)((char *)zero + YJSON_LONG_DIGITS) == 0;
#endif
    if (!int_ok || *(uint32_t *)((char *)negative + YJSON_LONG_DIGITS) != 5
        || ((uint64_t)((uint32_t *)((char *)wide_int + YJSON_LONG_DIGITS))[1] << 30) != (1ULL << 40)) {
        runtime_failure("int"); goto done;
    }
    double value;
    memcpy(&value, (char *)real + YJSON_FLOAT_VALUE, sizeof value);
    if (value != 1.5) { runtime_failure("float"); goto done; }
    if ((Py_ssize_t)WORD(list, YJSON_OB_SIZE) != 1 || ((PyObject **)WORD(list, YJSON_LIST_ITEMS))[0] != real) {
        runtime_failure("list"); goto done;
    }
    if ((Py_ssize_t)WORD(tuple, YJSON_OB_SIZE) != 1 || (PyObject *)WORD(tuple, YJSON_TUPLE_ITEMS) != real) {
        runtime_failure("tuple"); goto done;
    }
    if ((Py_ssize_t)WORD(bytes, YJSON_OB_SIZE) != 3 || memcmp((char *)bytes + YJSON_BYTES_DATA, "xyz", 4) != 0) {
        runtime_failure("bytes"); goto done;
    }
    /* dk_kind: DICT_KEYS_GENERAL (0) only when a key is not str. */
    if ((Py_ssize_t)WORD(text_keys, YJSON_DICT_USED) != 1 || ((unsigned char *)WORD(text_keys, YJSON_DICT_KEYS))[10] == 0
        || ((unsigned char *)WORD(general_keys, YJSON_DICT_KEYS))[10] != 0) {
        runtime_failure("dict"); goto done;
    }
    ok = 1;
done:
    Py_XDECREF(ascii); Py_XDECREF(wide); Py_XDECREF(negative); Py_XDECREF(zero); Py_XDECREF(wide_int);
    Py_XDECREF(real); Py_XDECREF(list); Py_XDECREF(tuple); Py_XDECREF(bytes); Py_XDECREF(text_keys); Py_XDECREF(general_keys);
    return ok;
}

PyObject *yjson_null(void) { restore_environment(); return NULL; }

typedef struct {
    uintptr_t context;
    PyObject *convert, *key_string, *fragment_type, *dataclass_fields_type, *uuid_type;
    PyObject *dataclass_name, *field_kind_name, *field_sentinel;
    PyObject *enum_type;
} ModuleState;

/* Per-call cache of dumps_socket(classify=...) answers, one per type seen. */
#define PLAN_CACHE_SIZE 16
/* Ancestors already in keepalive, per nesting level, so sibling callbacks skip re-pinning. */
#define PINNED_LEVELS 8

typedef struct {
    ModuleState *state;
    PyObject *capsule, *default_fn;
    long option;
    int conversions;
    int single_ancestor;
    PyObject *keepalive;
    PyObject *classify;  /* NULL: every unhandled object goes to default */
    struct RequestCache *cache;  /* allocated by the first callback that needs it */
} Request;

/* Callback-path state, kept out of Request so the hot path does not zero it per call. */
typedef struct RequestCache {
    int plan_count;
    PyTypeObject *plan_types[PLAN_CACHE_SIZE];
    PyObject *plans[PLAN_CACHE_SIZE];
    PyObject *pinned[PINNED_LEVELS];  /* borrowed: each is held by keepalive */
} RequestCache;

static RequestCache *request_cache(Request *request) {
    if (!request->cache && !(request->cache = PyMem_Calloc(1, sizeof(RequestCache)))) PyErr_NoMemory();
    return request->cache;
}

static PyObject *dumps(PyObject *, PyObject *const *, Py_ssize_t, PyObject *);
static PyObject *dumps_socket(PyObject *, PyObject *const *, Py_ssize_t, PyObject *);
static PyMethodDef dumps_method = {
    "dumps", (PyCFunction)(void (*)(void))dumps, METH_FASTCALL | METH_KEYWORDS,
    "dumps(obj, /, default=None, option=None) -> bytes"
};
static PyMethodDef socket_method = {
    "dumps_socket", (PyCFunction)(void (*)(void))dumps_socket, METH_FASTCALL | METH_KEYWORDS,
    "dumps_socket(obj, /, default=None, classify=None) -> bytes"
};

static void destroy_state(PyObject *capsule) {
    ModuleState *state = PyCapsule_GetPointer(capsule, "yjson.state");
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

static inline __attribute__((always_inline)) PyObject *dumps_impl(PyObject *capsule, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames, int socket, PyObject *classify) {
    if (nargs < 1 || nargs > 3) {
        PyErr_SetString(PyExc_TypeError, "dumps() requires one object and at most three positional arguments");
        return NULL;
    }
    ModuleState *state = PyCapsule_GetPointer(capsule, "yjson.state");
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
    Request request = {.state = state, .capsule = capsule, .default_fn = default_fn, .option = option, .classify = classify};
    PyObject *result = (PyObject *)yjson_encode(state->context, (uintptr_t)args[0], (uintptr_t)&request);
    Py_XDECREF(request.keepalive);
    if (request.cache) {
        for (int i = 0; i < request.cache->plan_count; i++) {
            Py_DECREF(request.cache->plans[i]);
            Py_DECREF(request.cache->plan_types[i]);
        }
        PyMem_Free(request.cache);
    }
    if (!result) wrap_error(0);
    return result;
}

static PyObject *dumps(PyObject *capsule, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    return dumps_impl(capsule, args, nargs, kwnames, 0, NULL);
}

static PyObject *dumps_socket(PyObject *capsule, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    if (nargs < 1 || nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "dumps_socket() accepts an object and default callback");
        return NULL;
    }
    PyObject *call[2] = {args[0], nargs == 2 ? args[1] : Py_None};
    PyObject *classify = NULL;
    int default_seen = nargs == 2;
    if (kwnames) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(kwnames); i++) {
            PyObject *name = PyTuple_GET_ITEM(kwnames, i);
            if (PyUnicode_CompareWithASCIIString(name, "default") == 0 && !default_seen) {
                call[1] = args[nargs + i]; default_seen = 1;
            } else if (PyUnicode_CompareWithASCIIString(name, "classify") == 0 && !classify) {
                classify = args[nargs + i];
            } else {
                PyErr_SetString(PyExc_TypeError, "dumps_socket() only accepts default= and classify=");
                return NULL;
            }
        }
    }
    if (classify == Py_None) classify = NULL;
    if (classify && !PyCallable_Check(classify)) {
        PyErr_SetString(PyExc_TypeError, "classify must be callable");
        return NULL;
    }
    return dumps_impl(capsule, call, 2, NULL, 1, classify);
}

/* Decimal conversion only for the cold, arbitrary-size integer path. CPython's
   formatter handles digit limits and switches algorithms for very large values. */
uintptr_t yjson_big_integer(uintptr_t object) {
    PyObject *obj = (PyObject *)object;
    Py_INCREF(obj);
    PyObject *result = PyLong_Type.tp_str(obj);
    Py_DECREF(obj);
    return (uintptr_t)result;
}

/* Preserve lone surrogates as JSON escapes, without invoking a JSON encoder. */
uintptr_t yjson_surrogate_string(uintptr_t object) {
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

long yjson_options(uintptr_t request) { return ((Request *)request)->option; }

int yjson_enter_fallback(uintptr_t request_ptr) {
    Request *request = (Request *)request_ptr;
    if (request->conversions >= 254) {
        PyErr_SetString(PyExc_TypeError, "default serializer recursion limit exceeded");
        return 0;
    }
    request->conversions++;
    return 1;
}

void yjson_leave_fallback(uintptr_t request) { ((Request *)request)->conversions--; }

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
        RequestCache *cache = request_cache(request);
        if (!cache) return 0;
        for (long i = 1; i <= depth; i++) Py_INCREF(ancestors[i]);
        PyObject *expanded = PyList_New(depth + 1);
        if (!expanded) {
            for (long i = 1; i <= depth; i++) Py_DECREF(ancestors[i]);
            return 0;
        }
        /* Transfer the existing and newly pinned references into the list. */
        PyList_SET_ITEM(expanded, 0, request->keepalive);
        for (long i = 1; i <= depth; i++) {
            PyList_SET_ITEM(expanded, i, ancestors[i]);
            if (i < PINNED_LEVELS) cache->pinned[i] = ancestors[i];
        }
        request->keepalive = expanded;
        request->single_ancestor = 0;
        return 1;
    }
    RequestCache *cache = request_cache(request);
    if (!cache) return 0;
    if (!request->keepalive) {
        /* Creating the list can run GC and arbitrary finalizers. Pin first. */
        for (long i = 1; i <= depth; i++) Py_INCREF(ancestors[i]);
        request->keepalive = PyList_New(0);
        int ok = request->keepalive != NULL;
        for (long i = 1; i <= depth; i++) {
            if (ok && PyList_Append(request->keepalive, ancestors[i]) < 0) ok = 0;
            else if (ok && i < PINNED_LEVELS) cache->pinned[i] = ancestors[i];
        }
        for (long i = 1; i <= depth; i++) Py_DECREF(ancestors[i]);
        return ok;
    }
    for (long i = 1; i <= depth; i++) {
        /* An object in keepalive stays alive for the whole call, so its address
           cannot be reused: pointer equality means it is already pinned. */
        if (i < PINNED_LEVELS && cache->pinned[i] == ancestors[i]) continue;
        if (PyList_Append(request->keepalive, ancestors[i]) < 0) return 0;
        if (i < PINNED_LEVELS) cache->pinned[i] = ancestors[i];
    }
    return 1;
}

int yjson_pin_ancestors(uintptr_t request, uintptr_t ancestors, long depth) {
    return retain_ancestors((Request *)request, ancestors, depth);
}

static void two_digits(char *dst, int value) {
    dst[0] = (char)('0' + value / 10);
    dst[1] = (char)('0' + value % 10);
}

/* Exact stdlib types have no user callbacks; custom tzinfo stays in the cold helper. */
static PyObject *datetime_string(PyObject *obj, long option, int *handled, char separator) {
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
        if (is_datetime) text[size++] = separator;
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

/* classify(type) answers once per type per call: a tuple of attribute names
   (write the object as that dict), a callable (write its result), or None
   (use default). Returns a borrowed plan, or NULL with an exception set. */
static PyObject *socket_plan(Request *request, PyTypeObject *type) {
    RequestCache *cache = request_cache(request);
    if (!cache) return NULL;
    for (int i = 0; i < cache->plan_count; i++)
        if (cache->plan_types[i] == type) return cache->plans[i];
    PyObject *plan = PyObject_CallOneArg(request->classify, (PyObject *)type);
    if (!plan) {
        if (PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1);
        return NULL;
    }
    int valid = plan == Py_None || PyCallable_Check(plan);
    if (!valid && PyTuple_CheckExact(plan)) {
        valid = 1;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(plan); i++)
            if (!PyUnicode_Check(PyTuple_GET_ITEM(plan, i))) { valid = 0; break; }
    }
    if (!valid) {
        Py_DECREF(plan);
        PyErr_SetString(PyExc_TypeError, "classify must return None, a callable, or a tuple of attribute names");
        return NULL;
    }
    if (cache->plan_count == PLAN_CACHE_SIZE) {
        /* Rare: more distinct types than slots. Keep the newest answer in the last slot. */
        Py_DECREF(cache->plans[PLAN_CACHE_SIZE - 1]);
        Py_DECREF(cache->plan_types[PLAN_CACHE_SIZE - 1]);
        cache->plan_count--;
    }
    /* Strong reference: a freed type's address could be reused by another type. */
    cache->plan_types[cache->plan_count] = (PyTypeObject *)Py_NewRef(type);
    cache->plans[cache->plan_count++] = plan;
    return plan;
}

static PyObject *attribute_dict(PyObject *obj, PyObject *names) {
    PyObject *result = _PyDict_NewPresized(PyTuple_GET_SIZE(names));
    if (!result) return NULL;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names); i++) {
        PyObject *name = PyTuple_GET_ITEM(names, i);
        PyObject *value = PyObject_GetAttr(obj, name);
        if (!value || PyDict_SetItem(result, name, value) < 0) {
            Py_XDECREF(value);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(value);
    }
    return result;
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
        PyObject *result = datetime_string(obj, request->option, &handled, 'T');
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
        if (request->classify) {
            PyObject *plan = socket_plan(request, Py_TYPE(obj));
            if (!plan) return 0;
            if (PyTuple_CheckExact(plan)) {
                *is_fragment = 2;
                return (uintptr_t)attribute_dict(obj, plan);
            }
            if (plan == (PyObject *)&PyUnicode_Type
                && (PyDate_CheckExact(obj) || PyDateTime_CheckExact(obj) || PyTime_CheckExact(obj))
                && !(PyDateTime_CheckExact(obj) && PyDateTime_DATE_GET_TZINFO(obj) != Py_None)
                && !(PyTime_CheckExact(obj) && PyDateTime_TIME_GET_TZINFO(obj) != Py_None)) {
                /* str() of a naive date/datetime/time is isoformat(" "), written without a method call. */
                int handled;
                return (uintptr_t)datetime_string(obj, 0, &handled, ' ');
            }
            if (plan != Py_None) {
                PyObject *result = PyObject_CallOneArg(plan, obj);
                if (!result && PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1);
                return (uintptr_t)result;
            }
        }
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

uintptr_t yjson_convert(uintptr_t request, uintptr_t object, int *kind, uintptr_t ancestors, long depth) {
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
        Request scalar = {.state = request->state, .capsule = request->capsule, .default_fn = Py_None};
        PyObject *bytes = (PyObject *)yjson_encode(request->state->context, (uintptr_t)key, (uintptr_t)&scalar);
        if (!bytes) return NULL;
        PyObject *text = PyUnicode_DecodeUTF8(PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes), "strict");
        Py_DECREF(bytes);
        return text;
    }
    int handled;
    PyObject *text = datetime_string(key, request->option, &handled, 'T');
    if (handled) return text;
    if (Py_IS_TYPE(key, (PyTypeObject *)request->state->uuid_type)) return uuid_string(key);
    PyObject *option = PyLong_FromLong(request->option);
    PyObject *native = PyCFunction_New(&dumps_method, request->capsule);
    if (!option || !native) { Py_XDECREF(option); Py_XDECREF(native); return NULL; }
    PyObject *result = PyObject_CallFunctionObjArgs(request->state->key_string, key, option, native, NULL);
    Py_DECREF(option); Py_DECREF(native);
    return result;
}

/* Dict records: one snapshot of (key, value, key text) per entry, built by
   walking the key table directly, sorted natively when asked, with non-str
   keys converted to text without creating Python strings. Both Mojo dict
   writers consume these. `key` and `value` are owned references; `key` is the
   original key, or a temporary str/bytes that owns the text. A negative
   length marks text that is already a complete JSON string (quoted and
   escaped), written verbatim. One trailing record holds the text arena. */
typedef struct {
    PyObject *key, *value;
    const char *utf8;
    Py_ssize_t length;
} DictRecord;

_Static_assert(sizeof(DictRecord) == 4 * sizeof(uintptr_t), "DictRecord ABI");

static inline int record_less(const DictRecord *a, const DictRecord *b) {
    /* UTF-8 byte order is code point order, so this matches PyUnicode_Compare. */
    Py_ssize_t la = a->length < 0 ? -a->length : a->length, lb = b->length < 0 ? -b->length : b->length;
    if (la && lb && a->utf8[0] != b->utf8[0]) return (unsigned char)a->utf8[0] < (unsigned char)b->utf8[0];
    int order = memcmp(a->utf8, b->utf8, (size_t)(la < lb ? la : lb));
    return order ? order < 0 : la < lb;
}

static void insertion_sort(DictRecord *items, Py_ssize_t count) {
    for (Py_ssize_t i = 1; i < count; i++) {
        DictRecord item = items[i];
        Py_ssize_t j = i;
        while (j && record_less(&item, items + j - 1)) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = item;
    }
}

/* Quicksort with median-of-three pivots, recursing on the smaller side only;
   small partitions finish with insertion sort. No comparison callbacks. */
static void sort_records(DictRecord *items, Py_ssize_t count) {
    while (count > 24) {
        Py_ssize_t mid = count / 2, last = count - 1;
        if (record_less(items + mid, items)) { DictRecord t = items[0]; items[0] = items[mid]; items[mid] = t; }
        if (record_less(items + last, items)) { DictRecord t = items[0]; items[0] = items[last]; items[last] = t; }
        if (record_less(items + last, items + mid)) { DictRecord t = items[mid]; items[mid] = items[last]; items[last] = t; }
        DictRecord pivot = items[mid];
        Py_ssize_t i = 0, j = last;
        for (;;) {
            while (record_less(items + i, &pivot)) i++;
            while (record_less(&pivot, items + j)) j--;
            if (i >= j) break;
            DictRecord t = items[i]; items[i] = items[j]; items[j] = t;
            i++; j--;
        }
        /* items[0..j] <= pivot <= items[j+1..] */
        Py_ssize_t left = j + 1, right = count - left;
        if (left < right) {
            sort_records(items, left);
            items += left; count = right;
        } else {
            sort_records(items + left, right);
            count = left;
        }
    }
    insertion_sort(items, count);
}

static void release_records(DictRecord *items, Py_ssize_t filled, char *arena, uintptr_t storage) {
    for (Py_ssize_t i = 0; i < filled; i++) {
        Py_DECREF(items[i].key);
        Py_DECREF(items[i].value);
    }
    PyMem_Free(arena);
    if ((uintptr_t)items != storage) PyMem_Free(items);
}

/* `count` is the dict size the records were built for; the arena follows the records. */
void yjson_release_records(uintptr_t items_ptr, long count, uintptr_t storage) {
    DictRecord *items = (DictRecord *)items_ptr;
    release_records(items, count, (char *)items[count].utf8, storage);
}

static int write_digits(char *dst, unsigned long long value) {
    char tmp[20];
    int n = 0;
    do { tmp[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (int i = 0; i < n; i++) dst[i] = tmp[n - 1 - i];
    return n;
}

/* Fills `record` for a non-str key. Returns 0 on error (exception set). */
static int nonstr_key_record(Request *request, PyObject *key, DictRecord *record, char *arena, Py_ssize_t *arena_used) {
    if (key == Py_None || key == Py_True || key == Py_False) {
        const char *text = key == Py_None ? "null" : key == Py_True ? "true" : "false";
        *record = (DictRecord){Py_NewRef(key), NULL, text, (Py_ssize_t)strlen(text)};
        return 1;
    }
    if (PyLong_CheckExact(key)) {
        int overflow = 0;
        long long value = PyLong_AsLongLongAndOverflow(key, &overflow);
        unsigned long long magnitude;
        int negative = 0;
        if (overflow == 0) {
            negative = value < 0;
            magnitude = negative ? 0ULL - (unsigned long long)value : (unsigned long long)value;
        } else if (overflow > 0 && ((magnitude = PyLong_AsUnsignedLongLong(key)) != (unsigned long long)-1 || !PyErr_Occurred())) {
            /* 2**63 <= key <= 2**64 - 1 */
        } else {
            PyErr_Clear();
            if (!(request->option & 65536)) {
                PyErr_SetString(PyExc_TypeError, "Integer exceeds 64-bit range");
                return 0;
            }
            PyObject *text = PyLong_Type.tp_str(key);  /* socket mode: any size */
            if (!text) return 0;
            Py_ssize_t length;
            const char *utf8 = PyUnicode_AsUTF8AndSize(text, &length);
            if (!utf8) { Py_DECREF(text); return 0; }
            *record = (DictRecord){text, NULL, utf8, length};
            return 1;
        }
        char *dst = arena + *arena_used;
        int n = 0;
        if (negative) dst[n++] = '-';
        n += write_digits(dst + n, magnitude);
        *arena_used += 21;
        *record = (DictRecord){Py_NewRef(key), NULL, dst, n};
        return 1;
    }
    if (PyFloat_CheckExact(key)) {
        double value = PyFloat_AS_DOUBLE(key);
        if (!isfinite(value)) {
            const char *text = !(request->option & 65536) ? "null" : isnan(value) ? "NaN" : value > 0 ? "Infinity" : "-Infinity";
            *record = (DictRecord){Py_NewRef(key), NULL, text, (Py_ssize_t)strlen(text)};
            return 1;
        }
        Request scalar = {.state = request->state, .capsule = request->capsule, .default_fn = Py_None};
        PyObject *bytes = (PyObject *)yjson_encode(request->state->context, (uintptr_t)key, (uintptr_t)&scalar);
        if (!bytes) return 0;
        *record = (DictRecord){bytes, NULL, PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes)};
        return 1;
    }
    PyObject *text = key_string(request, key);  /* datetime, uuid, enum, subclasses, ... */
    if (!text) return 0;
    Py_ssize_t length;
    const char *utf8 = PyUnicode_AsUTF8AndSize(text, &length);
    if (!utf8) { Py_DECREF(text); return 0; }
    *record = (DictRecord){text, NULL, utf8, length};
    return 1;
}

/* Fills `record` for a str key (exact or subclass). */
static int str_key_record(Request *request, PyObject *key, DictRecord *record) {
    Py_ssize_t length;
    const char *utf8;
    if (PyUnicode_IS_COMPACT_ASCII(key)) {
        utf8 = (const char *)PyUnicode_DATA(key);
        length = PyUnicode_GET_LENGTH(key);
    } else {
        utf8 = PyUnicode_AsUTF8AndSize(key, &length);
        if (!utf8) {
            if (!(request->option & 65536)) return 0;
            /* socket mode preserves lone surrogates as escapes: a complete JSON string */
            PyObject *escaped = (PyObject *)yjson_surrogate_string((uintptr_t)key);
            if (!escaped) return 0;
            *record = (DictRecord){escaped, NULL, PyBytes_AS_STRING(escaped), -PyBytes_GET_SIZE(escaped)};
            return 1;
        }
    }
    *record = (DictRecord){Py_NewRef(key), NULL, utf8, length};
    return 1;
}

uintptr_t yjson_dict_records(uintptr_t request_ptr, uintptr_t object, int sort, int nonstr, uintptr_t storage, long capacity,
                              uintptr_t ancestors, long depth) {
    Request *request = (Request *)request_ptr;
    PyDictObject *dict = (PyDictObject *)object;
    Py_ssize_t count = PyDict_GET_SIZE((PyObject *)dict), filled = 0, arena_used = 0;
    /* Key conversion may run Python code (enum, custom types): pin the containers. */
    if (nonstr && !retain_ancestors(request, ancestors, depth)) return 0;
    DictRecord *items = count + 1 <= capacity ? (DictRecord *)storage : PyMem_Malloc(((size_t)count + 1) * sizeof(DictRecord));
    if (!items) { PyErr_NoMemory(); return 0; }
    char *arena = NULL;
    if (nonstr && count) {
        arena = PyMem_Malloc((size_t)count * 21);
        if (!arena) { if ((uintptr_t)items != storage) PyMem_Free(items); PyErr_NoMemory(); return 0; }
    }
    items[count] = (DictRecord){NULL, NULL, arena, 0};
    PyDictKeysObject *keys = dict->ma_keys;
    int general = keys->dk_kind == DICT_KEYS_GENERAL;
    PyDictKeyEntry *entries = general ? DK_ENTRIES(keys) : NULL;
    PyDictUnicodeEntry *uentries = general ? NULL : DK_UNICODE_ENTRIES(keys);
    Py_ssize_t nentries = keys->dk_nentries, position = 0;
    int split = dict->ma_values != NULL;
    while (filled < count) {
        PyObject *key, *value;
        if (split) {
            if (!PyDict_Next((PyObject *)dict, &position, &key, &value)) break;
        } else {
            if (position >= nentries) break;
            if (general) { key = entries[position].me_key; value = entries[position].me_value; }
            else { key = uentries[position].me_key; value = uentries[position].me_value; }
            position++;
            if (!value) continue;
        }
        DictRecord *record = items + filled;
        int ok = PyUnicode_Check(key) ? str_key_record(request, key, record)
            : nonstr ? nonstr_key_record(request, key, record, arena, &arena_used)
            : (PyErr_SetString(PyExc_TypeError, "Dict key must be str"), 0);
        if (!ok) {
            release_records(items, filled, arena, storage);
            return 0;
        }
        record->value = Py_NewRef(value);
        filled++;
    }
    if (filled != count) {
        release_records(items, filled, arena, storage);
        PyErr_SetString(PyExc_TypeError, "Dictionary changed during serialization");
        return 0;
    }
    if (sort && count > 1) sort_records(items, count);
    return (uintptr_t)items;
}

int yjson_strict_integer(uintptr_t obj) {
    int overflow = 0;
    long long value = PyLong_AsLongLongAndOverflow((PyObject *)obj, &overflow);
    if (overflow || value < -9007199254740991LL || value > 9007199254740991LL) {
        PyErr_SetString(PyExc_TypeError, "Integer exceeds 53-bit range");
        return 0;
    }
    return !PyErr_Occurred();
}

void yjson_import_error(uintptr_t detail, Py_ssize_t length) {
    if (PyErr_Occurred()) return;
    PyObject *text = PyUnicode_DecodeUTF8((const char *)detail, length, "replace");
    if (!text) return;
    PyErr_Format(PyExc_ImportError, "yjson could not initialize its Mojo runtime: %U", text);
    Py_DECREF(text);
}

void yjson_error(int code) {
    if (PyErr_Occurred()) return;
    const char *message = "Integer exceeds 64-bit range or invalid UTF-8 string";
    if (code == 3) message = "Recursion limit exceeded";
    if (code == 4) message = "Dict key must be str";
    if (code == 5) message = "Unsupported type";
    if (code == 8) message = "Dictionary changed during serialization";
    PyErr_SetString(PyExc_TypeError, message);
}

int yjson_install(uintptr_t module_ptr, uintptr_t context) {
    restore_environment();
    PyObject *module = (PyObject *)module_ptr;
    PyDateTime_IMPORT;
    if (!PyDateTimeAPI) return -1;
    PyObject *support = PyImport_ImportModule("_yjson_support");
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
    PyObject *capsule = PyCapsule_New(state, "yjson.state", destroy_state);
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

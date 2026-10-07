#define PY_SSIZE_T_CLEAN
#define Py_BUILD_CORE 1
#include <Python.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "internal/pycore_dict.h"  /* key table layout, re-checked below */
#pragma GCC diagnostic pop
#undef Py_BUILD_CORE
#include <datetime.h>
#if PY_VERSION_HEX < 0x030C0000
#include <structmember.h>  /* PyMemberDef and T_OBJECT_EX before 3.12 */
#define Py_T_OBJECT_EX T_OBJECT_EX
#endif
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
YJSON_LAYOUT(YJSON_TP_FLAGS, offsetof(PyTypeObject, tp_flags));
/* datetime objects, written directly by the Mojo writer when naive or UTC */
YJSON_LAYOUT(YJSON_DT_HASTZ, offsetof(PyDateTime_DateTime, hastzinfo));
YJSON_LAYOUT(YJSON_DT_DATA, offsetof(PyDateTime_DateTime, data));
YJSON_LAYOUT(YJSON_DT_TZINFO, offsetof(PyDateTime_DateTime, tzinfo));
YJSON_LAYOUT(YJSON_DATE_DATA, offsetof(PyDateTime_Date, data));
YJSON_LAYOUT(YJSON_TIME_HASTZ, offsetof(PyDateTime_Time, hastzinfo));
YJSON_LAYOUT(YJSON_TIME_DATA, offsetof(PyDateTime_Time, data));
_Static_assert(_PyDateTime_DATETIME_DATASIZE == 10 && _PyDateTime_DATE_DATASIZE == 4 && _PyDateTime_TIME_DATASIZE == 6, "datetime data layout");
_Static_assert(Py_TPFLAGS_LONG_SUBCLASS == (1UL << 24) && Py_TPFLAGS_LIST_SUBCLASS == (1UL << 25) && Py_TPFLAGS_UNICODE_SUBCLASS == (1UL << 28)
    && Py_TPFLAGS_DICT_SUBCLASS == (1UL << 29), "type flag bits");
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

/* The way a tzinfo type answers for its offset (see tz_offset), remembered per type. */
#define TZ_CACHE_SIZE 4
enum { TZ_DATETIME_METHOD = 1, TZ_PYTZ_NORMALIZE, TZ_TZINFO_METHOD, TZ_UNSUPPORTED };
typedef struct {
    PyTypeObject *type;  /* strong reference: a freed type's address could be reused */
    int kind;
} TzCacheEntry;

typedef struct {
    uintptr_t context;
    PyObject *convert, *key_string, *fragment_type, *dataclass_fields_type, *uuid_type;
    PyObject *dataclass_name, *field_kind_name, *field_sentinel;
    PyObject *enum_type;
    PyObject *value_name, *slots_name, *utcoffset_name, *normalize_name, *convert_name, *dst_name;
    PyObject *value_private_name, *stock_value_descr;  /* "_value_" and enum.Enum.value, for the direct read */
    Py_ssize_t uuid_int_offset;  /* UUID.int slot, 0 when it is not a plain member slot */
    TzCacheEntry tz_cache[TZ_CACHE_SIZE];
    int tz_cache_next;
    TzCacheEntry dc_cache[TZ_CACHE_SIZE];  /* dataclass types: DC_DICT or DC_FIELDS */
    int dc_cache_next;
    /* Per type, under a given set of passthrough options: PLAIN_CALLBACK
       (no built-in conversion: classify/default), PLAIN_ENUM_VALUE (stock
       Enum: the member's _value_) or PLAIN_ENUM_PROPERTY (its value property). */
    struct { PyTypeObject *type; long options; int kind; } plain_cache[8];
    int plain_cache_next;
} ModuleState;
#define PLAIN_CACHE_SIZE 8
#define PLAIN_OPTIONS (256 | 512 | 2048 | 65536)
enum { PLAIN_CALLBACK = 1, PLAIN_ENUM_VALUE, PLAIN_ENUM_PROPERTY };

static void remember_type(ModuleState *state, PyTypeObject *type, long options, int kind) {
    for (int i = 0; i < PLAIN_CACHE_SIZE; i++)
        if (state->plain_cache[i].type == type && state->plain_cache[i].options == options) return;
    int slot = state->plain_cache_next;
    state->plain_cache_next = (slot + 1) % PLAIN_CACHE_SIZE;
    Py_XDECREF(state->plain_cache[slot].type);
    state->plain_cache[slot].type = (PyTypeObject *)Py_NewRef(type);
    state->plain_cache[slot].options = options;
    state->plain_cache[slot].kind = kind;
}

/* The value of a stock Enum member: _value_ from its dict, else the property. */
static PyObject *enum_member_value(ModuleState *state, PyObject *obj) {
    PyObject *dict = PyObject_GenericGetDict(obj, NULL);
    if (dict) {
        PyObject *value = PyDict_GetItemWithError(dict, state->value_private_name);  /* borrowed, owned by obj */
        Py_DECREF(dict);
        if (value) return Py_NewRef(value);
    }
    PyErr_Clear();
    return PyObject_GetAttr(obj, state->value_name);
}
enum { DC_DICT = 1, DC_FIELDS = 2 };
static ModuleState *module_state;  /* the one module instance (the Mojo context is also per process) */

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
/* The "--" line gives inspect.signature() a __text_signature__ (the $module placeholder stands for the bound capsule). */
static PyMethodDef dumps_method = {
    "dumps", (PyCFunction)(void (*)(void))dumps, METH_FASTCALL | METH_KEYWORDS,
    "dumps($module, obj, /, default=None, option=None)\n--\n\nSerialize obj to JSON bytes."
};
static PyMethodDef socket_method = {
    "dumps_socket", (PyCFunction)(void (*)(void))dumps_socket, METH_FASTCALL | METH_KEYWORDS,
    "dumps_socket($module, obj, /, default=None, classify=None)\n--\n\n"
    "Compact JSON with Python json semantics: big integers, NaN/Infinity, escaped lone surrogates."
};
#ifndef YJSON_VERSION
#define YJSON_VERSION "0.0.0"
#endif

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
    Py_XDECREF(state->value_name); Py_XDECREF(state->slots_name); Py_XDECREF(state->utcoffset_name);
    Py_XDECREF(state->normalize_name); Py_XDECREF(state->convert_name); Py_XDECREF(state->dst_name);
    Py_XDECREF(state->value_private_name); Py_XDECREF(state->stock_value_descr);
    for (int i = 0; i < TZ_CACHE_SIZE; i++) { Py_XDECREF(state->tz_cache[i].type); Py_XDECREF(state->dc_cache[i].type); }
    for (int i = 0; i < PLAIN_CACHE_SIZE; i++) Py_XDECREF(state->plain_cache[i].type);
    if (module_state == state) module_state = NULL;
    PyMem_Free(state);
}

/* A failing callback (default, classify, a plan) raises "Type is not JSON
   serializable: <type of obj>" with the callback's exception as __cause__, as
   orjson does; other non-TypeError failures keep their text. */
static void wrap_error(int callback, PyObject *obj) {
    if (!PyErr_Occurred()) {
        PyErr_SetString(PyExc_TypeError, "Unable to serialize object");
        return;
    }
    if ((!callback && (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_MemoryError)))
        || PyErr_ExceptionMatches(PyExc_KeyboardInterrupt) || PyErr_ExceptionMatches(PyExc_SystemExit)) return;
    PyObject *value = PyErr_GetRaisedException();
    PyObject *message = callback ? PyUnicode_FromFormat("Type is not JSON serializable: %s", Py_TYPE(obj)->tp_name) : PyObject_Str(value);
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
    if (nargs < 1) {
        PyErr_SetString(PyExc_TypeError, "dumps() missing 1 required positional argument: 'obj'");
        return NULL;
    }
    if (nargs > 3) {
        PyErr_SetString(PyExc_TypeError, "dumps() takes at most 3 positional arguments");
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
    if (!result) wrap_error(0, args[0]);
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
    if (request->conversions >= 255) {
        PyErr_SetString(PyExc_TypeError, "default serializer exceeds recursion limit");
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

/* How a tzinfo answers for its offset, following orjson: pendulum timezones
   (they have convert()) answer through the datetime, pytz zones (normalize())
   are normalized first so a zone attached with tzinfo= gets its real offset
   rather than LMT, and anything with dst() (dateutil, zoneinfo, every tzinfo
   subclass) answers utcoffset(dt). The answer is per type and remembered. */
static int has_attribute(PyObject *obj, PyObject *name) {
    PyObject *value = PyObject_GetAttr(obj, name);
    if (value) { Py_DECREF(value); return 1; }
    PyErr_Clear();
    return 0;
}

static int tz_kind(ModuleState *state, PyObject *tzinfo) {
    PyTypeObject *type = Py_TYPE(tzinfo);
    for (int i = 0; i < TZ_CACHE_SIZE; i++)
        if (state->tz_cache[i].type == type) return state->tz_cache[i].kind;
    int kind = TZ_UNSUPPORTED;
    if (type == Py_TYPE(PyDateTimeAPI->TimeZone_UTC) || has_attribute(tzinfo, state->convert_name)) kind = TZ_DATETIME_METHOD;
    else if (has_attribute(tzinfo, state->normalize_name)) kind = TZ_PYTZ_NORMALIZE;
    else if (has_attribute(tzinfo, state->dst_name)) kind = TZ_TZINFO_METHOD;
    TzCacheEntry *entry = &state->tz_cache[state->tz_cache_next];
    state->tz_cache_next = (state->tz_cache_next + 1) % TZ_CACHE_SIZE;
    Py_XDECREF(entry->type);
    entry->type = (PyTypeObject *)Py_NewRef(type);
    entry->kind = kind;
    return kind;
}

/* New reference to the offset (a timedelta or None), or NULL with an error set. */
static PyObject *tz_offset(ModuleState *state, PyObject *obj, PyObject *tzinfo) {
    switch (tz_kind(state, tzinfo)) {
    case TZ_DATETIME_METHOD:
        return PyObject_CallMethodNoArgs(obj, state->utcoffset_name);
    case TZ_PYTZ_NORMALIZE: {
        PyObject *normalized = PyObject_CallMethodOneArg(tzinfo, state->normalize_name, obj);
        if (!normalized) return NULL;
        PyObject *offset = PyObject_CallMethodNoArgs(normalized, state->utcoffset_name);
        Py_DECREF(normalized);
        return offset;
    }
    case TZ_TZINFO_METHOD:
        return PyObject_CallMethodOneArg(tzinfo, state->utcoffset_name, obj);
    default:
        PyErr_SetString(PyExc_TypeError, "datetime's timezone library is not supported: use datetime.timezone.utc, pendulum, pytz, or dateutil");
        return NULL;
    }
}

/* ISO text of an exact datetime/date/time into `text` (at least 40 bytes),
   without quotes. Returns the size, -1 with an error set, or 0 when obj is
   none of the three exact types. */
static int datetime_text(ModuleState *state, PyObject *obj, long option, char separator, char *text) {
    int is_datetime = PyDateTime_CheckExact(obj), is_time = PyTime_CheckExact(obj);
    if (!is_datetime && !is_time && !PyDate_CheckExact(obj)) return 0;
    PyObject *tzinfo = is_datetime ? PyDateTime_DATE_GET_TZINFO(obj) : (is_time ? PyDateTime_TIME_GET_TZINFO(obj) : Py_None);
    if (is_time && tzinfo != Py_None) {
        PyErr_SetString(PyExc_TypeError, "datetime.time must not have tzinfo set");
        return -1;
    }
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
        int offset_microseconds = 0, has_offset = 1;
        if (tzinfo != Py_None && tzinfo != PyDateTimeAPI->TimeZone_UTC) {
            PyObject *offset = tz_offset(state, obj, tzinfo);
            if (!offset) return -1;
            if (offset == Py_None) {
                has_offset = (option & 2) != 0;  /* the zone declines: written as naive */
            } else if (!PyDelta_Check(offset)) {
                Py_DECREF(offset);
                PyErr_SetString(PyExc_TypeError, "tzinfo.utcoffset() must return a timedelta or None");
                return -1;
            } else {
                seconds = (long)PyDateTime_DELTA_GET_DAYS(offset) * 86400 + PyDateTime_DELTA_GET_SECONDS(offset);
                offset_microseconds = PyDateTime_DELTA_GET_MICROSECONDS(offset);
            }
            Py_DECREF(offset);
        }
        if (has_offset && seconds == 0 && offset_microseconds == 0 && (option & 128)) {
            text[size++] = 'Z';
        } else if (has_offset) {
            text[size++] = seconds < 0 ? '-' : '+';
            if (seconds < 0) seconds = -seconds;
            int minutes = (int)((seconds + 30) / 60);
            two_digits(text + size, minutes / 60); text[size + 2] = ':';
            two_digits(text + size + 3, minutes % 60); size += 5;
        }
    }
    return size;
}

/* Exact stdlib types have no user callbacks; subclasses stay in the cold helper. */
static PyObject *datetime_string(ModuleState *state, PyObject *obj, long option, int *handled, char separator) {
    char text[48];
    int size = datetime_text(state, obj, option, separator, text);
    *handled = size != 0;
    return size > 0 ? PyUnicode_FromStringAndSize(text, size) : NULL;
}

/* For the Mojo writer: an aware datetime (any tzinfo) as a quoted JSON string
   at dst (48 bytes reserved). Returns the length, or -1 with an error set. */
int yjson_tz_datetime_text(uintptr_t request, uintptr_t object, char *dst) {
    Request *req = (Request *)request;
    dst[0] = '"';
    int size = datetime_text(req->state, (PyObject *)object, req->option, 'T', dst + 1);
    if (size <= 0) {
        if (size == 0) PyErr_SetString(PyExc_TypeError, "Unable to serialize object");
        return -1;
    }
    dst[size + 1] = '"';
    return size + 2;
}

/* Object addresses and layout facts the Mojo writer caches at import:
   0 datetime type, 1 date type, 2 time type, 3 the datetime.timezone.utc
   singleton, 4 uuid.UUID, 5 the byte offset of UUID.int (0 when unknown). */
uintptr_t yjson_special_types(int which) {
    ModuleState *state = module_state;
    switch (which) {
    case 0: return (uintptr_t)PyDateTimeAPI->DateTimeType;
    case 1: return (uintptr_t)PyDateTimeAPI->DateType;
    case 2: return (uintptr_t)PyDateTimeAPI->TimeType;
    case 3: return (uintptr_t)PyDateTimeAPI->TimeZone_UTC;
    case 4: return state ? (uintptr_t)state->uuid_type : 0;
    case 5: return state ? (uintptr_t)state->uuid_int_offset : 0;
    case 6: return offsetof(Request, conversions);  /* the writer keeps the default-nesting count itself */
    default: return 0;
    }
}

/* UUID stores its value in the `int` slot; find the slot's offset so the
   writer reads it without an attribute lookup. */
static Py_ssize_t slot_offset(PyTypeObject *type, const char *name) {
#if PY_VERSION_HEX >= 0x030C0000
    PyObject *dict = PyType_GetDict(type);
#else
    PyObject *dict = Py_XNewRef(type->tp_dict);
#endif
    if (!dict) { PyErr_Clear(); return 0; }
    PyObject *descr = PyDict_GetItemString(dict, name);  /* borrowed */
    Py_ssize_t offset = 0;
    if (descr && Py_IS_TYPE(descr, &PyMemberDescr_Type)) {
        PyMemberDef *member = ((PyMemberDescrObject *)descr)->d_member;
        if (member && member->type == Py_T_OBJECT_EX && member->offset > 0) offset = member->offset;
    }
    Py_DECREF(dict);
    PyErr_Clear();
    return offset;
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

static PyObject *dataclass_fields(ModuleState *state, PyObject *obj, PyObject *schema);

/* How a dataclass type is written, remembered per type (strong reference):
   DC_DICT when instances have a __dict__ and the class declares no
   __slots__ (orjson's fast path: the dict's order, attributes not starting
   with "_"), DC_FIELDS otherwise, 0 when the type is not a dataclass. */
static int dataclass_kind(ModuleState *state, PyTypeObject *type) {
    for (int i = 0; i < TZ_CACHE_SIZE; i++)
        if (state->dc_cache[i].type == type) return state->dc_cache[i].kind;
    PyObject *schema = _PyType_Lookup(type, state->dataclass_name);
    if (!schema || !PyDict_Check(schema)) return 0;
    int kind = DC_FIELDS;
    if (type->tp_dictoffset != 0) {
#if PY_VERSION_HEX >= 0x030C0000
        PyObject *type_dict = PyType_GetDict(type);
#else
        PyObject *type_dict = Py_XNewRef(type->tp_dict);
#endif
        int has_slots = type_dict ? PyDict_Contains(type_dict, state->slots_name) : 1;
        Py_XDECREF(type_dict);
        if (has_slots < 0) { PyErr_Clear(); has_slots = 1; }
        if (!has_slots) kind = DC_DICT;
    }
    TzCacheEntry *entry = &state->dc_cache[state->dc_cache_next];
    state->dc_cache_next = (state->dc_cache_next + 1) % TZ_CACHE_SIZE;
    Py_XDECREF(entry->type);
    entry->type = (PyTypeObject *)Py_NewRef(type);
    entry->kind = kind;
    return kind;
}

/* The dict to write for a dataclass instance. *is_fragment: 3 = the instance
   dict itself, whose "_" keys the Mojo writer skips; 2 = a dict holding
   exactly the attributes to write. */
static PyObject *dataclass_dict(ModuleState *state, PyObject *obj, int kind, int filter, int *is_fragment) {
    *is_fragment = 2;
    if (kind == DC_DICT) {
        PyObject *dict = PyObject_GenericGetDict(obj, NULL);
        if (!dict) {
            PyErr_Clear();
        } else if (!filter) {
            *is_fragment = 3;
            return dict;
        } else {
            PyObject *result = _PyDict_NewPresized(PyDict_GET_SIZE(dict));
            Py_ssize_t position = 0;
            PyObject *key, *value;
            while (result && PyDict_Next(dict, &position, &key, &value)) {
                if (!PyUnicode_Check(key)) {
                    Py_CLEAR(result);
                    PyErr_SetString(PyExc_TypeError, "Dict key must be str");
                    break;
                }
                if (PyUnicode_GET_LENGTH(key) && PyUnicode_READ_CHAR(key, 0) == '_') continue;
                if (PyDict_SetItem(result, key, value) < 0) Py_CLEAR(result);
            }
            Py_DECREF(dict);
            return result;
        }
    }
    PyObject *schema = _PyType_Lookup(Py_TYPE(obj), state->dataclass_name);
    if (!schema || !PyDict_Check(schema)) {
        PyErr_Format(PyExc_TypeError, "Type is not JSON serializable: %s", Py_TYPE(obj)->tp_name);
        return NULL;
    }
    return dataclass_fields(state, obj, schema);
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
        if (PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1, (PyObject *)type);
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


/* ---- NumPy ----
   Arrays are read through the array interface (__array_struct__), whose
   layout has been fixed since the protocol's version 2. */
typedef struct {
    int two;              /* always 2 */
    int nd;
    char typekind;        /* 'b' 'i' 'u' 'f' 'c' 'M' 'm' 'O' 'S' 'U' 'V' */
    int itemsize;
    int flags;
    Py_ssize_t *shape, *strides;
    void *data;
    PyObject *descr;
} PyArrayInterface;
#define NPY_ARRAY_C_CONTIGUOUS 0x0001
#define NPY_ARRAY_NOTSWAPPED 0x0200

/* NPY_DATETIMEUNIT, as numpy numbers it (3 is unused). */
enum { NPY_FR_Y = 0, NPY_FR_M = 1, NPY_FR_W = 2, NPY_FR_D = 4, NPY_FR_h = 5, NPY_FR_m = 6, NPY_FR_s = 7, NPY_FR_ms = 8,
       NPY_FR_us = 9, NPY_FR_ns = 10, NPY_FR_ps = 11, NPY_FR_fs = 12, NPY_FR_as = 13, NPY_FR_GENERIC = 14 };
static const char *const datetime_unit_names[] = {"years", "months", "weeks", "", "days", "hours", "minutes", "seconds",
    "milliseconds", "microseconds", "nanoseconds", "picoseconds", "femtoseconds", "attoseconds", "generic"};

/* Unit of a datetime64 dtype from its type string ("<M8[ns]"; "<M8" is the
   generic unit). -1 with a TypeError set for anything else, such as "10s". */
static int datetime_unit(PyObject *dtype) {
    static const struct { const char *code; int unit; } units[] = {
        {"Y", NPY_FR_Y}, {"M", NPY_FR_M}, {"W", NPY_FR_W}, {"D", NPY_FR_D}, {"h", NPY_FR_h}, {"m", NPY_FR_m}, {"s", NPY_FR_s},
        {"ms", NPY_FR_ms}, {"us", NPY_FR_us}, {"ns", NPY_FR_ns}, {"ps", NPY_FR_ps}, {"fs", NPY_FR_fs}, {"as", NPY_FR_as},
        {"generic", NPY_FR_GENERIC}};
    PyObject *text = PyObject_GetAttrString(dtype, "str");
    if (!text) return -1;
    const char *s = PyUnicode_Check(text) ? PyUnicode_AsUTF8(text) : NULL;
    if (!s) {
        Py_DECREF(text);
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_TypeError, "numpy array is malformed");
        return -1;
    }
    int unit = -1;
    const char *open = strchr(s, '[');
    if (!open) {
        unit = NPY_FR_GENERIC;
    } else {
        const char *close = strchr(open, ']');
        size_t n = close ? (size_t)(close - open - 1) : strlen(open + 1);
        for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
            if (strlen(units[i].code) == n && memcmp(units[i].code, open + 1, n) == 0) unit = units[i].unit;
        }
        if (unit < 0) PyErr_Format(PyExc_TypeError, "unsupported numpy.datetime64 unit: %.*s", (int)n, open + 1);
    }
    Py_DECREF(text);
    return unit;
}

static long long floor_div(long long a, long long b) {
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* Proleptic Gregorian date of a day count from 1970-01-01 (H. Hinnant's civil_from_days). */
static void civil_from_days(long long z, long long *year, int *month, int *day) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = (long long)yoe + era * 400 + (*month <= 2);
}

static int datetime64_unrepresentable(long long value, int unit) {
    PyErr_Format(PyExc_TypeError, "unrepresentable numpy.datetime64: %lld %s", value, datetime_unit_names[unit]);
    return -1;
}

/* Writes a quoted "YYYY-MM-DDTHH:MM:SS[.ffffff][+00:00|Z]" for a datetime64
   value in `unit`, as orjson does (years 0000-9999, sub-microsecond digits
   dropped, OPT_OMIT_MICROSECONDS / OPT_NAIVE_UTC / OPT_UTC_Z applied).
   Returns the length, or -1 with a TypeError set. NaT is unrepresentable. */
int yjson_datetime64_text(long long value, int unit, long option, char *dst) {
    long long year, days = 0, seconds = 0, microsecond = 0;
    int month = 1, day = 1;
    switch (unit) {
    case NPY_FR_Y:
        year = value + 1970;
        if (year < 0 || year > 9999) return datetime64_unrepresentable(value, unit);
        break;
    case NPY_FR_M:
        year = 1970 + floor_div(value, 12);
        month = (int)(value - floor_div(value, 12) * 12) + 1;
        if (year < 0 || year > 9999) return datetime64_unrepresentable(value, unit);
        break;
    case NPY_FR_W:
        if (__builtin_mul_overflow(value, 7LL, &days)) return datetime64_unrepresentable(value, unit);
        goto from_days;
    case NPY_FR_D:
        days = value;
        goto from_days;
    case NPY_FR_h:
        days = floor_div(value, 24); seconds = (value - days * 24) * 3600;
        goto from_days;
    case NPY_FR_m:
        days = floor_div(value, 1440); seconds = (value - days * 1440) * 60;
        goto from_days;
    case NPY_FR_s:
        days = floor_div(value, 86400); seconds = value - days * 86400;
        goto from_days;
    case NPY_FR_ms: case NPY_FR_us: case NPY_FR_ns: {
        long long per_second = unit == NPY_FR_ms ? 1000 : unit == NPY_FR_us ? 1000000 : 1000000000;
        long long whole = floor_div(value, per_second), fraction = value - whole * per_second;
        microsecond = unit == NPY_FR_ms ? fraction * 1000 : unit == NPY_FR_us ? fraction : fraction / 1000;
        days = floor_div(whole, 86400); seconds = whole - days * 86400;
        goto from_days;
    }
    default:
        PyErr_Format(PyExc_TypeError, "unsupported numpy.datetime64 unit: %s", datetime_unit_names[unit]);
        return -1;
    from_days:
        if (days < -719528 || days > 2932896) return datetime64_unrepresentable(value, unit);  /* 0000-01-01 .. 9999-12-31 */
        civil_from_days(days, &year, &month, &day);
    }
    int n = 0;
    dst[n++] = '"';
    two_digits(dst + n, (int)(year / 100)); two_digits(dst + n + 2, (int)(year % 100)); n += 4;
    dst[n++] = '-'; two_digits(dst + n, month); n += 2;
    dst[n++] = '-'; two_digits(dst + n, day); n += 2;
    dst[n++] = 'T'; two_digits(dst + n, (int)(seconds / 3600)); n += 2;
    dst[n++] = ':'; two_digits(dst + n, (int)(seconds / 60 % 60)); n += 2;
    dst[n++] = ':'; two_digits(dst + n, (int)(seconds % 60)); n += 2;
    if (microsecond && !(option & 8)) {
        dst[n++] = '.';
        for (int i = 5; i >= 0; i--) { dst[n + i] = (char)('0' + microsecond % 10); microsecond /= 10; }
        n += 6;
    }
    if (option & 2) {
        if (option & 128) dst[n++] = 'Z';
        else { memcpy(dst + n, "+00:00", 6); n += 6; }
    }
    dst[n++] = '"';
    return n;
}

/* A datetime64 scalar as a complete JSON string (bytes), or NULL with an error.
   The value sits after the object header (PyDatetimeScalarObject.obval);
   the unit comes from the dtype. */
static PyObject *numpy_datetime64_scalar(PyObject *obj, long option) {
    if (Py_TYPE(obj)->tp_basicsize < (Py_ssize_t)(sizeof(PyObject) + sizeof(long long))) {
        PyErr_SetString(PyExc_TypeError, "numpy.datetime64 scalar is malformed");
        return NULL;
    }
    long long value;
    memcpy(&value, (char *)obj + sizeof(PyObject), sizeof value);
    PyObject *dtype = PyObject_GetAttrString(obj, "dtype");
    if (!dtype) return NULL;
    int unit = datetime_unit(dtype);
    Py_DECREF(dtype);
    if (unit < 0) return NULL;
    char text[48];
    int n = yjson_datetime64_text(value, unit, option, text);
    return n < 0 ? NULL : PyBytes_FromStringAndSize(text, n);
}

static PyArrayInterface *array_interface(PyObject *obj, PyObject **capsule) {
    *capsule = PyObject_GetAttrString(obj, "__array_struct__");
    if (!*capsule) return NULL;
    PyArrayInterface *array = PyCapsule_GetPointer(*capsule, NULL);
    if (!array || array->two != 2) {
        Py_CLEAR(*capsule);
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_TypeError, "numpy array is malformed");
        return NULL;
    }
    return array;
}

/* datetime64 arrays export no buffer. Fills info (data, ndim, shape, unit,
   capsule, element count) for a C-contiguous native-order one and returns 1;
   the capsule keeps the array alive until the writer releases it. 0 means
   the fallback decides (not datetime64, non-contiguous, 0-d); -1 an error. */
int yjson_numpy_datetime_array(uintptr_t request, uintptr_t object, uintptr_t *info) {
    (void)request;
    PyObject *obj = (PyObject *)object;
    PyObject *dtype = PyObject_GetAttrString(obj, "dtype");
    if (!dtype) return -1;
    PyObject *kind = PyObject_GetAttrString(dtype, "kind");
    if (!kind) { Py_DECREF(dtype); return -1; }
    int is_datetime = PyUnicode_Check(kind) && PyUnicode_CompareWithASCIIString(kind, "M") == 0;
    Py_DECREF(kind);
    int unit = is_datetime ? datetime_unit(dtype) : 0;
    Py_DECREF(dtype);
    if (!is_datetime) return 0;
    if (unit < 0) return -1;
    PyObject *capsule;
    PyArrayInterface *array = array_interface(obj, &capsule);
    if (!array) return -1;
    if ((array->flags & NPY_ARRAY_C_CONTIGUOUS) == 0 || (array->flags & NPY_ARRAY_NOTSWAPPED) == 0 || array->nd < 1
        || array->itemsize != 8 || array->typekind != 'M') {
        Py_DECREF(capsule);
        return 0;
    }
    Py_ssize_t count = 1;
    for (int d = 0; d < array->nd; d++) count *= array->shape[d];
    info[0] = (uintptr_t)array->data;
    info[1] = (uintptr_t)array->nd;
    info[2] = (uintptr_t)array->shape;
    info[3] = (uintptr_t)unit;
    info[4] = (uintptr_t)capsule;
    info[5] = (uintptr_t)count;
    return 1;
}

static int numpy_dtype_supported(char typekind, int itemsize) {
    switch (typekind) {
    case 'b': return itemsize == 1;
    case 'i': case 'u': return itemsize == 1 || itemsize == 2 || itemsize == 4 || itemsize == 8;
    case 'f': return itemsize == 2 || itemsize == 4 || itemsize == 8;
    case 'M': return itemsize == 8;
    default: return 0;
    }
}

/* Why the native writers declined an ndarray, with orjson's wording and
   whether default may take it. *message stays NULL for an array they should
   have taken. Returns 0 with an error set when the array cannot be read. */
static int numpy_array_reason(PyObject *obj, const char **message, int *can_default) {
    PyObject *capsule;
    PyArrayInterface *array = array_interface(obj, &capsule);
    if (!array) return 0;
    *message = NULL;
    *can_default = 1;
    if ((array->flags & NPY_ARRAY_C_CONTIGUOUS) == 0) {
        *message = "numpy array is not C contiguous; use ndarray.tolist() in default";
    } else if ((array->flags & NPY_ARRAY_NOTSWAPPED) == 0) {
        *message = "numpy array is not native-endianness";
        *can_default = 0;
    } else if (array->nd == 0 || !numpy_dtype_supported(array->typekind, array->itemsize)) {
        *message = "unsupported datatype in numpy array";
    }
    Py_DECREF(capsule);
    return 1;
}

static uintptr_t convert_object(uintptr_t request_ptr, uintptr_t object, int *is_fragment, uintptr_t ancestors_ptr, long depth) {
    Request *request = (Request *)request_ptr;
    PyObject *obj = (PyObject *)object;
    if (Py_IS_TYPE(obj, (PyTypeObject *)request->state->fragment_type)) {
        *is_fragment = 1;
        PyObject *data = PyObject_GetAttrString(obj, "_data");
        if (data && !PyBytes_CheckExact(data)) {
            /* Fragment() keeps a str it could not encode so the error surfaces here, as orjson's does. */
            PyErr_SetString(PyExc_TypeError, PyUnicode_Check(data) ? "str is not valid UTF-8: surrogates not allowed"
                                                                   : "Fragment requires bytes or str");
            Py_DECREF(data);
            return 0;
        }
        return (uintptr_t)data;
    }
    *is_fragment = 0;
    ModuleState *state = request->state;
    PyTypeObject *type = Py_TYPE(obj);
    long plain_options = request->option & PLAIN_OPTIONS;
    for (int i = 0; i < PLAIN_CACHE_SIZE; i++) {
        if (state->plain_cache[i].type == type && state->plain_cache[i].options == plain_options) {
            if (!retain_ancestors(request, ancestors_ptr, depth)) return 0;
            int kind = state->plain_cache[i].kind;
            if (kind == PLAIN_ENUM_VALUE) return (uintptr_t)enum_member_value(state, obj);
            if (kind == PLAIN_ENUM_PROPERTY) return (uintptr_t)PyObject_GetAttr(obj, state->value_name);
            goto callback;
        }
    }
    if (!(request->option & 2048) && !PyType_Check(obj)) {
        int kind = dataclass_kind(request->state, Py_TYPE(obj));
        if (kind) {
            /* Reading the instance runs no Python code, but writing its values may: pin the containers. */
            if (!retain_ancestors(request, ancestors_ptr, depth)) return 0;
            /* The generic option traversal has no "_"-skipping dict writer: give it a filtered dict. */
            return (uintptr_t)dataclass_dict(request->state, obj, kind, (request->option & (64 | 256)) != 0, is_fragment);
        }
    }
    if (!(request->option & 512)) {
        int handled;
        PyObject *result = datetime_string(request->state, obj, request->option, &handled, 'T');
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
        if (!result && PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1, obj);
        return (uintptr_t)result;
    }
    /* str/int/list/dict subclasses before Enum, as orjson orders them: an
       IntEnum or StrEnum is written as its int or str value. */
    if (!(request->option & 256)) {
        if (PyUnicode_Check(obj)) return (uintptr_t)PyUnicode_FromObject(obj);
        if (PyLong_Check(obj)) return (uintptr_t)_PyLong_Copy((PyLongObject *)obj);
        if (PyList_Check(obj)) return (uintptr_t)PyList_GetSlice(obj, 0, PyList_GET_SIZE(obj));
        if (PyDict_Check(obj)) return (uintptr_t)PyDict_Copy(obj);
    }
    if (PyObject_TypeCheck(obj, (PyTypeObject *)request->state->enum_type)) {
        /* The stock Enum.value property returns the member's _value_: read it
           without the Python-level descriptor call. An overridden value
           property is honored through the normal lookup. Per type, remembered. */
        int stock = state->stock_value_descr && type->tp_dictoffset != 0
            && _PyType_Lookup(type, state->value_name) == state->stock_value_descr;
        remember_type(state, type, plain_options, stock ? PLAIN_ENUM_VALUE : PLAIN_ENUM_PROPERTY);
        return (uintptr_t)(stock ? enum_member_value(state, obj) : PyObject_GetAttr(obj, state->value_name));
    }
    /* The Mojo writers take numeric scalars and C-contiguous arrays of the
       supported dtypes directly. datetime64 scalars are formatted here; an
       array they declined follows orjson's rules (default, or an error naming
       why); other numpy types (complex, timedelta64, void, ...) go to default. */
    const char *type_name = Py_TYPE(obj)->tp_name;
    if (type_name[0] == 'n' && strncmp(type_name, "numpy.", 6) == 0) {
        if (strcmp(type_name + 6, "datetime64") == 0) {
            *is_fragment = 1;
            return (uintptr_t)numpy_datetime64_scalar(obj, request->option);
        }
        if (strcmp(type_name + 6, "ndarray") == 0) {
            const char *message = NULL;
            int can_default = 0;
            if (!numpy_array_reason(obj, &message, &can_default)) return 0;
            if (!message) return (uintptr_t)PyObject_CallMethod(obj, "tolist", NULL);
            if (!can_default || request->default_fn == Py_None) {
                PyErr_SetString(PyExc_TypeError, message);
                return 0;
            }
        }
    }
    /* datetime subclasses (pendulum, arrow) are formatted by the Python helper; dataclasses returned above. */
    if (!(!(request->option & 512) && (PyDate_Check(obj) || PyTime_Check(obj)))) {
        /* Nothing built in applies to this type under these options: remember
           that. numpy arrays decide per instance (contiguity, dtype), so not them. */
        if (!(type_name[0] == 'n' && strncmp(type_name, "numpy.", 6) == 0)) remember_type(state, type, plain_options, PLAIN_CALLBACK);
    callback:
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
                return (uintptr_t)datetime_string(request->state, obj, 0, &handled, ' ');
            }
            if (plan != Py_None) {
                PyObject *result = PyObject_CallOneArg(plan, obj);
                if (!result && PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1, obj);
                return (uintptr_t)result;
            }
        }
        if (request->default_fn == Py_None) {
            PyErr_Format(PyExc_TypeError, "Type is not JSON serializable: %s", Py_TYPE(obj)->tp_name);
            return 0;
        }
        PyObject *result = PyObject_CallOneArg(request->default_fn, obj);
        if (!result && PyErr_ExceptionMatches(PyExc_Exception)) wrap_error(1, obj);
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
    PyObject *text = datetime_string(request->state, key, request->option, &handled, 'T');
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

int yjson_install_loads(PyObject *module, PyObject *support);  /* src/decoder_api.c */

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
    state->value_name = PyUnicode_InternFromString("value");
    state->slots_name = PyUnicode_InternFromString("__slots__");
    state->utcoffset_name = PyUnicode_InternFromString("utcoffset");
    state->normalize_name = PyUnicode_InternFromString("normalize");
    state->convert_name = PyUnicode_InternFromString("convert");
    state->dst_name = PyUnicode_InternFromString("dst");
    state->value_private_name = PyUnicode_InternFromString("_value_");
    if (state->enum_type && PyType_Check(state->enum_type)) {
#if PY_VERSION_HEX >= 0x030C0000
        PyObject *enum_dict = PyType_GetDict((PyTypeObject *)state->enum_type);
#else
        PyObject *enum_dict = Py_XNewRef(((PyTypeObject *)state->enum_type)->tp_dict);
#endif
        if (enum_dict) {
            state->stock_value_descr = Py_XNewRef(PyDict_GetItemString(enum_dict, "value"));
            Py_DECREF(enum_dict);
        }
        PyErr_Clear();
    }
    if (state->uuid_type && PyType_Check(state->uuid_type)) state->uuid_int_offset = slot_offset((PyTypeObject *)state->uuid_type, "int");
    PyObject *capsule = PyCapsule_New(state, "yjson.state", destroy_state);
    if (!capsule) {
        Py_XDECREF(state->convert); Py_XDECREF(state->key_string); Py_XDECREF(state->fragment_type);
        Py_XDECREF(state->dataclass_fields_type);
        Py_XDECREF(state->uuid_type);
        Py_XDECREF(state->dataclass_name); Py_XDECREF(state->field_kind_name); Py_XDECREF(state->field_sentinel);
        Py_XDECREF(state->enum_type);
        Py_XDECREF(state->value_name); Py_XDECREF(state->slots_name); Py_XDECREF(state->utcoffset_name);
        Py_XDECREF(state->normalize_name); Py_XDECREF(state->convert_name); Py_XDECREF(state->dst_name);
        Py_XDECREF(state->value_private_name); Py_XDECREF(state->stock_value_descr);
        PyMem_Free(state); Py_DECREF(support); return -1;
    }
    if (!state->convert || !state->key_string || !state->fragment_type || !state->dataclass_fields_type || !state->uuid_type
        || !state->dataclass_name || !state->field_kind_name || !state->field_sentinel || !state->enum_type
        || !state->value_name || !state->slots_name || !state->utcoffset_name || !state->normalize_name || !state->convert_name
        || !state->dst_name || !state->value_private_name) goto fail;
    module_state = state;
    PyObject *module_name = PyUnicode_FromString("yjson"), *version = PyUnicode_FromString(YJSON_VERSION);
    if (!module_name || !version) { Py_XDECREF(module_name); Py_XDECREF(version); goto fail; }
    int status = PyObject_SetAttrString(module, "__version__", version);
    Py_DECREF(version);
    if (status < 0) { Py_DECREF(module_name); goto fail; }
    PyObject *func = PyCFunction_NewEx(&dumps_method, capsule, module_name);
    if (!func) { Py_DECREF(module_name); goto fail; }
    status = PyObject_SetAttrString(module, "dumps", func);
    Py_DECREF(func);
    if (status < 0) { Py_DECREF(module_name); goto fail; }
    func = PyCFunction_NewEx(&socket_method, capsule, module_name);
    Py_DECREF(module_name);
    if (!func) goto fail;
    status = PyObject_SetAttrString(module, "dumps_socket", func);
    Py_DECREF(func);
    if (status < 0) goto fail;
    if (yjson_install_loads(module, support) < 0) goto fail;
    PyObject *dict = PyModule_GetDict(support), *key, *value;
    Py_ssize_t position = 0;
    while (PyDict_Next(dict, &position, &key, &value)) {
        const char *name = PyUnicode_AsUTF8(key);
        if (!name) goto fail;
        if (strncmp(name, "OPT_", 4) == 0 || strcmp(name, "Fragment") == 0
            || strcmp(name, "JSONEncodeError") == 0 || strcmp(name, "JSONDecodeError") == 0) {
            if (PyObject_SetAttr(module, key, value) < 0) goto fail;
        }
    }
    Py_DECREF(capsule); Py_DECREF(support);
    return 0;
fail:
    Py_DECREF(capsule); Py_DECREF(support);
    return -1;
}

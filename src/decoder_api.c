/* CPython binding and compiled annotation plans for the Mojo JSON parser.
   JSON scanning, parsing and typed traversal live entirely in decoder.mojo. */
#define PY_SSIZE_T_CLEAN
#include "decoder_api.h"
#include <stdio.h>
#include <string.h>
#if PY_VERSION_HEX < 0x030C0000
#include <structmember.h>
#define Py_T_OBJECT_EX T_OBJECT_EX
#define Py_READONLY READONLY
#endif
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
static const char *const MSG_UTF8 = "str is not valid UTF-8: surrogates not allowed";
static PyObject *decode_error_type;
extern intptr_t yjson_mojo_init(intptr_t, intptr_t, intptr_t);
extern intptr_t yjson_mojo_loads_typed(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
extern void yjson_mojo_destroy(intptr_t);
typedef struct { intptr_t context; } DecoderState;
static DecoderState *decoder_state;   /* the one decoder, set when loads is installed */
static void destroy_decoder(PyObject *capsule) {
    DecoderState *state = PyCapsule_GetPointer(capsule, "yjson.decoder");
    if (state == decoder_state) decoder_state = NULL;
    if (state) { yjson_mojo_destroy(state->context); PyMem_Free(state); }
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

int yjson_decoder_set_field(PyObject *obj, const Field *field, PyObject *value) {
    if (field->slot >= 0) {
        Py_XSETREF(*(PyObject **)((char *)obj + field->slot), Py_NewRef(value));
        return 0;
    }
    return PyObject_GenericSetAttr(obj, field->name, value);
}

PyObject *yjson_decoder_alloc(const Plan *plan) {
    return plan->cls->tp_alloc(plan->cls, 0);
}
int yjson_decoder_is_int(PyObject *value) { return PyLong_CheckExact(value); }
PyObject *yjson_decoder_post_init(PyObject *obj) {
    return PyObject_CallMethodNoArgs(obj, post_init_name);
}

static const char *decode_message(intptr_t code) {
    switch (code) {
    case 1: return "unexpected end of data";
    case 2: return "unexpected character, expected a JSON value";
    case 3: return "str is not valid UTF-8: surrogates not allowed";
    case 4: return "invalid escaped sequence in string";
    case 5: return "unexpected control character in string";
    case 6: return "no low surrogate in string";
    case 7: return "number with leading zero is not allowed";
    case 8: return "unexpected character in number, expected a digit";
    case 9: return "number is infinity when parsed as double";
    case 10: return "recursion limit exceeded";
    case 11: return "trailing comma is not allowed";
    case 12: return "unexpected character, expected ',' or ']'";
    case 13: return "unexpected character, expected a string key";
    case 14: return "unexpected character, expected ':' after key";
    case 15: return "Input is a zero-length, empty document";
    case 16: return "UTF-8 byte order mark (BOM) is not supported";
    case 17: return "unexpected content after document";
    case 19: return "invalid low surrogate in string";
    case 20: return "lone low surrogate in string";
    case 23: return "unexpected character, expected ',' or '}'";
    case 24: return "no digit after sign";
    default: return NULL;
    }
}

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

static PyObject *type_keyword;        /* interned "type" */
static PyObject *last_annotation;     /* strong: the annotation of the last typed call */
static const Plan *last_plan;

static PyObject *loads(PyObject *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    DecoderState *state = decoder_state;
    if (!state) {
        state = PyCapsule_GetPointer(self, "yjson.decoder");
        if (!state) return NULL;
    }
    if (nargs != 1) {
        if (nargs == 0) PyErr_SetString(PyExc_TypeError, "loads() missing 1 required positional argument: 'obj'");
        else PyErr_Format(PyExc_TypeError, "loads() takes exactly 1 positional argument (%zd given)", nargs);
        return NULL;
    }
    PyObject *obj = args[0], *annotation = NULL;
    for (Py_ssize_t i = 0; kwnames && i < PyTuple_GET_SIZE(kwnames); i++) {
        PyObject *name = PyTuple_GET_ITEM(kwnames, i);
        if (name != type_keyword && PyUnicode_CompareWithASCIIString(name, "type") != 0) {
            PyErr_Format(PyExc_TypeError, "loads() got an unexpected keyword argument '%U'", name);
            return NULL;
        }
        annotation = args[nargs + i];
    }
    const Plan *plan = NULL;
    if (annotation && annotation != Py_None) {
        if (annotation == last_annotation) {
            plan = last_plan;
        } else {
            plan = plan_for(annotation);
            if (!plan) return NULL;
            if (plan->kind == PLAN_ANY) plan = NULL;
            Py_XSETREF(last_annotation, Py_NewRef(annotation));
            last_plan = plan;
        }
    }
    const unsigned char *data;
    Py_ssize_t length;
    Py_buffer view;
    PyObject *source = NULL;  /* the input when it is already a str */
    PyObject *copy = NULL;    /* an immutable snapshot of a mutable buffer */
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
        copy = PyBytes_FromStringAndSize(view.buf, view.len);  /* NUL-terminated and immutable */
        PyBuffer_Release(&view);
        if (!copy) return NULL;
        data = (const unsigned char *)PyBytes_AS_STRING(copy);
        length = PyBytes_GET_SIZE(copy);
    } else {
        raise_decode_error("Input must be bytes, bytearray, memoryview, or str", NULL, (const unsigned char *)"", 0, 0);
        return NULL;
    }

    intptr_t error[4] = {0, 0, 0, 0};
    PyObject *result = (PyObject *)yjson_mojo_loads_typed(state->context,
        (intptr_t)data, length, (intptr_t)plan, (intptr_t)error);
    if (!result && !PyErr_Occurred()) {
        char message[200];
        const char *text = decode_message(error[0]);
        if (error[0] == 21) {
            const Plan *expected = (const Plan *)error[2];
            const char *got = error[3] ? "float" : token_name(data + error[1], data + length);
            snprintf(message, sizeof message, "expected %s, got %s", expected->name, got);
            text = message;
        } else if (error[0] == 22) {
            const Plan *expected = (const Plan *)error[2];
            const Field *field = (const Field *)error[3];
            const char *name = PyUnicode_AsUTF8(field->name);
            if (name) {
                snprintf(message, sizeof message, "missing required field '%s' of %s", name, expected->name);
                text = message;
            }
        }
        if (!PyErr_Occurred()) {
            if (text) raise_decode_error(text, source, data, length, error[1]);
            else PyErr_NoMemory();
        }
    }
    Py_XDECREF(copy);
    return result;
}

/* The "--" line gives inspect.signature() a __text_signature__. */
static PyMethodDef loads_method = {"loads", (PyCFunction)(void (*)(void))loads, METH_FASTCALL | METH_KEYWORDS,
    "loads($module, obj, /, *, type=None)\n--\n\nDeserialize JSON to Python objects, or to `type` when given."};

int yjson_install_loads(PyObject *module, PyObject *support) {
    PyObject *error_type = PyObject_GetAttrString(support, "JSONDecodeError");
    if (!error_type) return -1;
    Py_XSETREF(decode_error_type, error_type);
    PyObject *describe = PyObject_GetAttrString(support, "describe_type");
    if (!describe) return -1;
    Py_XSETREF(describe_type, describe);
    if (!plan_cache) plan_cache = PyDict_New();
    if (!post_init_name) post_init_name = PyUnicode_InternFromString("__post_init__");
    if (!type_keyword) type_keyword = PyUnicode_InternFromString("type");
    if (!plan_cache || !post_init_name || !type_keyword) return -1;
    DecoderState *state = PyMem_Calloc(1, sizeof(*state));
    if (!state) { PyErr_NoMemory(); return -1; }
    state->context = yjson_mojo_init((intptr_t)Py_True, (intptr_t)Py_False, (intptr_t)Py_None);
    if (!state->context) { PyMem_Free(state); PyErr_NoMemory(); return -1; }
    PyObject *capsule = PyCapsule_New(state, "yjson.decoder", destroy_decoder);
    if (!capsule) { yjson_mojo_destroy(state->context); PyMem_Free(state); return -1; }
    decoder_state = state;
    PyObject *module_name = PyUnicode_FromString("yjson");
    if (!module_name) { Py_DECREF(capsule); return -1; }
    PyObject *func = PyCFunction_NewEx(&loads_method, capsule, module_name);
    Py_DECREF(module_name);
    Py_DECREF(capsule);
    if (!func) return -1;
    int status = PyObject_SetAttrString(module, "loads", func);
    Py_DECREF(func);
    return status;
}

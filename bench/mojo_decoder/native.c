/* Native Python entry point for the benchmark parser. The GIL stays held. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>

extern intptr_t yjson_mojo_init(intptr_t, intptr_t, intptr_t);
extern intptr_t yjson_mojo_loads(intptr_t, intptr_t, intptr_t, intptr_t);
extern void yjson_mojo_destroy(intptr_t);

typedef struct { intptr_t context; } DecoderState;

static PyObject *loads(PyObject *module, PyObject *obj) {
    const char *data;
    Py_ssize_t length;
    PyObject *copy = NULL;
    if (PyBytes_CheckExact(obj)) {
        data = PyBytes_AS_STRING(obj);
        length = PyBytes_GET_SIZE(obj);
    } else if (PyUnicode_CheckExact(obj)) {
        data = PyUnicode_AsUTF8AndSize(obj, &length);
        if (!data) return NULL;
    } else if (PyByteArray_CheckExact(obj) || PyMemoryView_Check(obj)) {
        Py_buffer view;
        if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0) {
            PyErr_Clear();
            PyErr_SetString(PyExc_ValueError, "Input buffer must be contiguous");
            return NULL;
        }
        /* The parser's digit chains rely on an immutable, NUL-terminated input. */
        copy = PyBytes_FromStringAndSize(view.buf, view.len);
        PyBuffer_Release(&view);
        if (!copy) return NULL;
        data = PyBytes_AS_STRING(copy);
        length = PyBytes_GET_SIZE(copy);
    } else {
        PyErr_SetString(PyExc_ValueError, "Input must be bytes, bytearray, memoryview, or str");
        return NULL;
    }
    DecoderState *state = PyModule_GetState(module);
    intptr_t error[2] = {0, 0};
    PyObject *result = (PyObject *)yjson_mojo_loads(state->context, (intptr_t)data,
        length, (intptr_t)error);
    Py_XDECREF(copy);
    if (!result && !PyErr_Occurred()) {
        if (error[0] == 18 || error[0] == 0) return PyErr_NoMemory();
        PyErr_Format(PyExc_ValueError, "decode error %zd at byte %zd", error[0], error[1]);
    }
    return result;
}

static void free_module(void *module) {
    DecoderState *state = PyModule_GetState((PyObject *)module);
    if (state && state->context) yjson_mojo_destroy(state->context);
}

static PyMethodDef methods[] = {
    {"loads", loads, METH_O, "Decode JSON using the standalone Mojo parser."},
    {NULL, NULL, 0, NULL}
};
static int exec_module(PyObject *result) {
    DecoderState *state = PyModule_GetState(result);
    state->context = yjson_mojo_init((intptr_t)Py_True, (intptr_t)Py_False, (intptr_t)Py_None);
    if (!state->context) { PyErr_NoMemory(); return -1; }
    return PyModule_AddObjectRef(result, "JSONDecodeError", PyExc_ValueError);
}
static PyModuleDef_Slot slots[] = {
    {Py_mod_exec, exec_module},
    {0, NULL}
};
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_mojo_decoder", NULL, sizeof(DecoderState), methods,
    slots, NULL, NULL, free_module
};
PyMODINIT_FUNC PyInit__mojo_decoder(void) {
    return PyModuleDef_Init(&module);
}

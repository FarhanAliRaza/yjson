/* The former production decoder, built only as a benchmark baseline. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
extern int yjson_install_loads(PyObject *, PyObject *);
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_c_decoder", "C decoding benchmark baseline.", -1,
    NULL, NULL, NULL, NULL, NULL
};
PyMODINIT_FUNC PyInit__c_decoder(void) {
    PyObject *result = PyModule_Create(&module);
    if (!result) return NULL;
    PyObject *support = PyImport_ImportModule("_yjson_support");
    if (!support) { Py_DECREF(result); return NULL; }
    int status = yjson_install_loads(result, support);
    PyObject *error = status == 0 ? PyObject_GetAttrString(support, "JSONDecodeError") : NULL;
    Py_DECREF(support);
    if (status < 0 || !error) { Py_DECREF(result); return NULL; }
    status = PyModule_AddObjectRef(result, "JSONDecodeError", error);
    Py_DECREF(error);
    if (status < 0) { Py_DECREF(result); return NULL; }
    return result;
}

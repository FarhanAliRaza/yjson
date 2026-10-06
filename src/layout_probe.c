/* Prints the CPython object layout the Mojo encoder reads directly, as shell
   assignments. build.sh compiles this against the target interpreter's headers
   and passes every value to both compilers: python_api.c re-checks them with
   _Static_assert and mojson.mojo consumes them as compile-time constants. */
#define PY_SSIZE_T_CLEAN
#define Py_BUILD_CORE 1
#include <Python.h>
#include "internal/pycore_dict.h"
#include <stddef.h>
#include <stdio.h>

int main(void) {
    printf("MOJSON_PY_MINOR=%d\n", PY_MINOR_VERSION);
    printf("MOJSON_OB_TYPE=%zu\n", offsetof(PyObject, ob_type));
    printf("MOJSON_OB_SIZE=%zu\n", offsetof(PyVarObject, ob_size));
    printf("MOJSON_TP_NAME=%zu\n", offsetof(PyTypeObject, tp_name));
    printf("MOJSON_FLOAT_VALUE=%zu\n", offsetof(PyFloatObject, ob_fval));
#if PY_VERSION_HEX >= 0x030C0000
    /* 3.12+: lv_tag = ndigits << 3 | sign */
    printf("MOJSON_LONG_TAGGED=1\n");
    printf("MOJSON_LONG_TAG=%zu\n", offsetof(PyLongObject, long_value.lv_tag));
    printf("MOJSON_LONG_DIGITS=%zu\n", offsetof(PyLongObject, long_value.ob_digit));
#else
    /* 3.11: ob_size = signed digit count */
    printf("MOJSON_LONG_TAGGED=0\n");
    printf("MOJSON_LONG_TAG=%zu\n", offsetof(PyVarObject, ob_size));
    printf("MOJSON_LONG_DIGITS=%zu\n", offsetof(PyLongObject, ob_digit));
#endif
    printf("MOJSON_LIST_ITEMS=%zu\n", offsetof(PyListObject, ob_item));
    printf("MOJSON_TUPLE_ITEMS=%zu\n", offsetof(PyTupleObject, ob_item));
    printf("MOJSON_BYTES_DATA=%zu\n", offsetof(PyBytesObject, ob_sval));
    printf("MOJSON_DICT_USED=%zu\n", offsetof(PyDictObject, ma_used));
    printf("MOJSON_DICT_KEYS=%zu\n", offsetof(PyDictObject, ma_keys));
    printf("MOJSON_DICT_VALUES=%zu\n", offsetof(PyDictObject, ma_values));
    printf("MOJSON_DK_LOG2_INDEX_BYTES=%zu\n", offsetof(PyDictKeysObject, dk_log2_index_bytes));
    printf("MOJSON_DK_KIND=%zu\n", offsetof(PyDictKeysObject, dk_kind));
    printf("MOJSON_DK_NENTRIES=%zu\n", offsetof(PyDictKeysObject, dk_nentries));
    printf("MOJSON_DK_INDICES=%zu\n", offsetof(PyDictKeysObject, dk_indices));
    printf("MOJSON_STR_LENGTH=%zu\n", offsetof(PyASCIIObject, length));
    printf("MOJSON_STR_STATE=%zu\n", offsetof(PyASCIIObject, state));
    printf("MOJSON_STR_ASCII_DATA=%zu\n", sizeof(PyASCIIObject));
    printf("MOJSON_STR_UTF8_LENGTH=%zu\n", offsetof(PyCompactUnicodeObject, utf8_length));
    printf("MOJSON_STR_UTF8=%zu\n", offsetof(PyCompactUnicodeObject, utf8));
    return 0;
}

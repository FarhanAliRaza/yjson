/* Prints the CPython object layout the Mojo encoder reads directly, as shell
   assignments. build.sh compiles this against the target interpreter's headers
   and passes every value to both compilers: python_api.c re-checks them with
   _Static_assert and yjson.mojo consumes them as compile-time constants. */
#define PY_SSIZE_T_CLEAN
#define Py_BUILD_CORE 1
#include <Python.h>
#include "internal/pycore_dict.h"
#include <datetime.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
    printf("YJSON_PY_MINOR=%d\n", PY_MINOR_VERSION);
    printf("YJSON_OB_TYPE=%zu\n", offsetof(PyObject, ob_type));
    printf("YJSON_OB_SIZE=%zu\n", offsetof(PyVarObject, ob_size));
    printf("YJSON_TP_NAME=%zu\n", offsetof(PyTypeObject, tp_name));
    printf("YJSON_TP_FLAGS=%zu\n", offsetof(PyTypeObject, tp_flags));
    printf("YJSON_FLOAT_VALUE=%zu\n", offsetof(PyFloatObject, ob_fval));
#if PY_VERSION_HEX >= 0x030C0000
    /* 3.12+: lv_tag = ndigits << 3 | sign */
    printf("YJSON_LONG_TAGGED=1\n");
    printf("YJSON_LONG_TAG=%zu\n", offsetof(PyLongObject, long_value.lv_tag));
    printf("YJSON_LONG_DIGITS=%zu\n", offsetof(PyLongObject, long_value.ob_digit));
#else
    /* 3.11: ob_size = signed digit count */
    printf("YJSON_LONG_TAGGED=0\n");
    printf("YJSON_LONG_TAG=%zu\n", offsetof(PyVarObject, ob_size));
    printf("YJSON_LONG_DIGITS=%zu\n", offsetof(PyLongObject, ob_digit));
#endif
    printf("YJSON_LIST_ITEMS=%zu\n", offsetof(PyListObject, ob_item));
    printf("YJSON_TUPLE_ITEMS=%zu\n", offsetof(PyTupleObject, ob_item));
    printf("YJSON_BYTES_DATA=%zu\n", offsetof(PyBytesObject, ob_sval));
    printf("YJSON_DICT_USED=%zu\n", offsetof(PyDictObject, ma_used));
    printf("YJSON_DICT_KEYS=%zu\n", offsetof(PyDictObject, ma_keys));
    printf("YJSON_DICT_VALUES=%zu\n", offsetof(PyDictObject, ma_values));
    printf("YJSON_DK_LOG2_INDEX_BYTES=%zu\n", offsetof(PyDictKeysObject, dk_log2_index_bytes));
    printf("YJSON_DK_KIND=%zu\n", offsetof(PyDictKeysObject, dk_kind));
    printf("YJSON_DK_NENTRIES=%zu\n", offsetof(PyDictKeysObject, dk_nentries));
    printf("YJSON_DK_INDICES=%zu\n", offsetof(PyDictKeysObject, dk_indices));
    printf("YJSON_STR_LENGTH=%zu\n", offsetof(PyASCIIObject, length));
    printf("YJSON_STR_STATE=%zu\n", offsetof(PyASCIIObject, state));
    printf("YJSON_STR_ASCII_DATA=%zu\n", sizeof(PyASCIIObject));
    printf("YJSON_STR_UTF8_LENGTH=%zu\n", offsetof(PyCompactUnicodeObject, utf8_length));
    printf("YJSON_STR_UTF8=%zu\n", offsetof(PyCompactUnicodeObject, utf8));
    /* datetime objects: a tzinfo flag, packed big-endian fields, the tzinfo pointer */
    printf("YJSON_DT_HASTZ=%zu\n", offsetof(PyDateTime_DateTime, hastzinfo));
    printf("YJSON_DT_DATA=%zu\n", offsetof(PyDateTime_DateTime, data));
    printf("YJSON_DT_TZINFO=%zu\n", offsetof(PyDateTime_DateTime, tzinfo));
    printf("YJSON_DATE_DATA=%zu\n", offsetof(PyDateTime_Date, data));
    printf("YJSON_TIME_HASTZ=%zu\n", offsetof(PyDateTime_Time, hastzinfo));
    printf("YJSON_TIME_DATA=%zu\n", offsetof(PyDateTime_Time, data));
    return 0;
}

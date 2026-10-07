/* CPython schema ABI consumed by decoder.mojo; offsets are probed at build time. */
#ifndef YJSON_DECODER_API_H
#define YJSON_DECODER_API_H
#include <Python.h>
#include <stdint.h>
typedef struct Plan Plan;
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


#endif

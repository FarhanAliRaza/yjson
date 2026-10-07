# yjson

[![PyPI](https://img.shields.io/pypi/v/yjson.svg)](https://pypi.org/project/yjson/)
[![Python versions](https://img.shields.io/pypi/pyversions/yjson.svg)](https://pypi.org/project/yjson/)
[![CI](https://github.com/FarhanAliRaza/yjson/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/FarhanAliRaza/yjson/actions/workflows/ci.yml)

A fast JSON serializer for CPython, written in [Mojo](https://mojolang.org) and compatible
with [orjson](https://github.com/ijl/orjson).

- About 1.3× faster than orjson at serializing a corpus of real-world JSON documents, and
  about as fast at parsing them.
- A drop-in replacement for orjson, with the same API, options and output, verified against
  orjson's own test suite.
- Native support for dataclasses, `datetime`, `UUID`, `Enum`, and NumPy arrays and scalars.
- One deliberate difference in output: `NaN` and `Infinity` are written the way Python's
  `json` module writes them, not as `null`.

## Installation

```bash
pip install yjson
```

Wheels are available for CPython 3.11–3.15 on x86-64 Linux with glibc 2.35 or newer
(Ubuntu 22.04 or later), and require a CPU with AVX2. They bundle the Mojo runtime and have
no dependencies. Releases before 0.2.0 installed the module as `mojson`.

## Quickstart

```python
import yjson

yjson.dumps({"id": 1, "tags": ["a", "b"], "score": float("nan")})
# b'{"id":1,"tags":["a","b"],"score":NaN}'

yjson.dumps({"b": 1, "a": 2}, option=yjson.OPT_SORT_KEYS | yjson.OPT_INDENT_2).decode()
# '{\n  "a": 2,\n  "b": 1\n}'

yjson.loads(b'{"id": 1}')
# {'id': 1}
```

Code written for orjson can usually switch by changing the import (`import yjson as orjson`);
see *Differences from orjson* below. More examples are in
[`examples/usage.py`](https://github.com/FarhanAliRaza/yjson/blob/main/examples/usage.py).

## Serialization

```python
dumps(obj, /, default=None, option=None) -> bytes
```

`dumps` returns compact UTF-8 JSON as `bytes`; call `.decode()` if you need a `str`. These
types are serialized natively:

| Type | Output |
| --- | --- |
| `str`, `int`, `float`, `bool`, `None` | JSON string, number, `true`/`false`, `null` |
| `dict`, `list`, `tuple` | JSON object or array |
| Subclasses of `str`, `int`, `dict`, `list` | As their base type |
| Dataclass instances | Object of their fields, skipping names that start with `_` |
| `datetime.datetime`, `date`, `time` | RFC 3339 string, e.g. `"2024-01-02T03:04:05+01:00"` |
| `uuid.UUID` | String, e.g. `"00000000-0000-0000-0000-000000000001"` |
| `enum.Enum` | The member's value |
| NumPy arrays and scalars | See *NumPy* below |
| `yjson.Fragment` | Its contents, inserted verbatim |

Integers must fit in 64 bits (`-2**63` to `2**64 - 1`), and dict keys must be strings unless
`OPT_NON_STR_KEYS` is set. Aware datetimes work with `datetime.timezone`, `zoneinfo`,
`dateutil`, `pendulum` and `pytz`.

Any other object is passed to `default`, and its return value is serialized in its place:

```python
import decimal

yjson.dumps({"amount": decimal.Decimal("12.50")}, default=str)
# b'{"amount":"12.50"}'
```

`dumps` raises `JSONEncodeError`, an alias of `TypeError`, for unsupported types, invalid
keys, out-of-range integers, strings that are not valid UTF-8, and circular or too deeply
nested data. If `default` raises, its exception is attached as `__cause__`.

### Options

Options have the same names and values as orjson's, and are combined with `|`:

| Option | Effect |
| --- | --- |
| `OPT_APPEND_NEWLINE` | Append `\n` to the output. |
| `OPT_INDENT_2` | Pretty-print with two-space indentation. |
| `OPT_NAIVE_UTC` | Serialize naive `datetime` objects as UTC. |
| `OPT_NON_STR_KEYS` | Allow `int`, `float`, `bool`, `None`, `datetime`, `date`, `time`, `UUID` and `Enum` keys. |
| `OPT_OMIT_MICROSECONDS` | Drop microseconds from `datetime` and `time` values. |
| `OPT_PASSTHROUGH_DATACLASS` | Pass dataclass instances to `default`. |
| `OPT_PASSTHROUGH_DATETIME` | Pass `datetime`, `date` and `time` objects to `default`. |
| `OPT_PASSTHROUGH_SUBCLASS` | Pass subclasses of `str`, `int`, `dict` and `list` to `default`. |
| `OPT_SERIALIZE_NUMPY` | Accepted for compatibility; NumPy values are always serialized. |
| `OPT_SORT_KEYS` | Sort dict keys. |
| `OPT_STRICT_INTEGER` | Reject integers outside `-(2**53 - 1)` to `2**53 - 1`, JavaScript's safe integer range. |
| `OPT_UTC_Z` | Write a UTC offset as `Z` instead of `+00:00`. |

`OPT_SERIALIZE_DATACLASS` and `OPT_SERIALIZE_UUID` are deprecated and equal to `0`, as in
orjson. `OPT_STRICT_INTEGER` and `OPT_PASSTHROUGH_SUBCLASS` use a slower, generic code path;
all other options run on the same optimized writers as the default.

### Fragments

`Fragment` embeds JSON that is already serialized, such as a cached value:

```python
yjson.dumps({"cached": yjson.Fragment(b'{"a":1}')})
# b'{"cached":{"a":1}}'
```

The contents are inserted as they are, without validation.

## Deserialization

```python
loads(obj, /) -> Any
```

`loads` accepts `str`, `bytes`, `bytearray` or `memoryview`, and raises `JSONDecodeError`, a
subclass of `json.JSONDecodeError` and `ValueError`, on invalid input. It is a strict parser
with orjson's rules: it rejects invalid UTF-8, lone surrogates, `NaN` and `Infinity`, a byte
order mark, trailing commas and nesting deeper than 1,024 levels, and returns integers outside
[-2⁶³, 2⁶⁴) as floats. The error's `pos`, `lineno` and `colno` give the position at which
parsing stopped, as orjson reports it.

## NumPy

Arrays and scalars of type `bool`, `int8`–`int64`, `uint8`–`uint64`, `float16`, `float32`,
`float64` and `datetime64` are serialized directly from memory, without `.tolist()` or
`OPT_SERIALIZE_NUMPY`:

```python
import numpy as np

yjson.dumps({"matrix": np.array([[1.5, np.nan], [3.0, -np.inf]]), "count": np.int64(4)})
# b'{"matrix":[[1.5,NaN],[3.0,-Infinity]],"count":4}'
```

- `float16` and `float32` values are written with their own shortest round-trip digits, so
  `np.float32(0.1)` is written as `0.1`.
- `datetime64` values are written as `YYYY-MM-DDTHH:MM:SS[.ffffff]`, and honor the datetime
  options.
- Arrays that are not C-contiguous, are zero-dimensional or have another dtype are passed to
  `default`, or raise `JSONEncodeError` without one. Arrays in non-native byte order always
  raise. These rules match orjson's.

## `dumps_socket`

```python
dumps_socket(obj, /, default=None, classify=None) -> bytes
```

A second encoder, built for [Reflex](https://reflex.dev)'s Socket.IO messages. It follows
the semantics of Python's `json` module rather than orjson's: integers of any size, `NaN`
and `Infinity` tokens, and lone surrogates escaped instead of rejected. It writes compact
UTF-8 and takes no options. Objects beyond plain JSON, such as `datetime`, dataclass and
`UUID` instances, go to `default` unless `classify` handles them.

`classify(type)` lets a framework serialize its own types without a Python call per object.
yjson calls it once for each type that would otherwise reach `default`, and caches the answer
for the rest of the call:

- A tuple of attribute names writes the object as `{name: getattr(obj, name), ...}`.
- A callable is called with the object, and its result is written. When the callable is `str`
  and the object is a naive `date`, `time` or `datetime`, yjson formats the value itself,
  matching `str()`.
- `None` passes the object to `default`.

```python
import dataclasses

@dataclasses.dataclass
class Row:
    id: int
    name: str

def classify(cls):
    if dataclasses.is_dataclass(cls):
        return tuple(field.name for field in dataclasses.fields(cls))
    return None

yjson.dumps_socket([Row(1, "a"), Row(2, "b")], classify=classify)
# b'[{"id":1,"name":"a"},{"id":2,"name":"b"}]'
```

## Differences from orjson

`dumps` produces the same bytes as orjson, and CI runs orjson 3.12.0's test suite against
yjson. The deliberate differences are:

- `NaN`, `Infinity` and `-Infinity` are written as Python's `json` module writes them, where
  orjson writes `null`. Strict parsers, including `yjson.loads`, orjson and JavaScript's
  `JSON.parse`, reject these tokens; Python's `json.loads` accepts them.
- NumPy values are serialized without `OPT_SERIALIZE_NUMPY`.
- Wheels are available only for x86-64 Linux with AVX2.
- `dumps_socket` has no orjson equivalent.

## Performance

Speedup over orjson 3.12.0 when serializing documents from the
[simdjson-data](https://github.com/simdjson/simdjson-data) corpus (higher is better):

| Document | 3.11 | 3.12 | 3.13 | 3.14 | 3.15 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `twitter.json` | 1.38× | 1.39× | 1.37× | 1.36× | 1.41× |
| `github_events.json` | 1.44× | 1.32× | 1.38× | 1.34× | 1.35× |
| `citm_catalog.json` | 1.21× | 1.26× | 1.24× | 1.28× | 1.26× |
| `canada.json` | 1.07× | 1.12× | 1.04× | 1.05× | 1.09× |
| All 14 documents (geometric mean) | 1.28× | 1.30× | 1.29× | 1.31× | 1.31× |

Measured with `bench/bench_paired.py`, which checks that both libraries produce identical
output, then times them in 40 alternating pairs per document on one pinned core of an Intel
Xeon at 2.80 GHz (AVX2). Of the other libraries measured (msgspec, ujson, python-rapidjson,
simplejson and `json`), orjson was the fastest.

Results depend on the data. yjson can be slower than orjson on some shapes, such as lists of
short strings or of mixed `True`, `False` and `None`, and the two perform about the same when
most of the time goes to Python `default` functions. The
[full results](https://github.com/FarhanAliRaza/yjson/blob/main/bench/results/README.md)
cover every document, per-shape measurements, other libraries and the Reflex benchmarks;
[CONTRIBUTING.md](https://github.com/FarhanAliRaza/yjson/blob/main/CONTRIBUTING.md#benchmarks)
explains how to measure your own payloads.

Parsing the same corpus with `loads`, the speedup over `orjson.loads` on CPython 3.13 ranges
from 0.88× (`numbers.json`) to 1.95× (`gsoc-2018.json`), with a geometric mean of 1.10× over
the 14 documents; `json.loads` is 2–5× slower than either. Measured with
`bench/bench_loads.py` in 20 alternating pairs per document on a shared cloud machine, so
differences under about 5% are noise.

## How it works

yjson is a CPython extension module written in Mojo, with a C shim for the Python C API.
Instead of calling the C API for each value, its writers read CPython's object layouts
directly: list and tuple items, compact integers, float bits, string data and dict key
tables. The field offsets are probed from the target interpreter's headers at build time and
checked against live objects at import, so an unsupported interpreter fails with an
`ImportError` rather than misreading memory. Floats are formatted with the
[Żmij](https://github.com/vitaut/zmij) shortest round-trip algorithm, integers with an
[itoap](https://github.com/Kogia-sima/itoap)-style writer in 4-wide SIMD batches, and
strings are escaped with a 64-byte SIMD scan. Output is written straight into the resulting
`bytes` object.

`loads` is a separate recursive-descent parser in C (`src/decoder.c`) that shares no code with
the encoder. It scans strings 16 bytes at a time, builds `str` objects straight from the input
(plain ASCII by copy, anything else through CPython's UTF-8 decoder, which also validates it),
reuses recently seen object keys from a cache so repeated keys share one `str` and its hash,
and parses floats with the [Eisel-Lemire](https://arxiv.org/abs/2101.11408) algorithm as
implemented in [fast_float](https://github.com/fastfloat/fast_float), falling back to
CPython's `strtod` for numbers with more than 19 significant digits.

## Limitations

- x86-64 Linux only. The extension targets x86-64-v3 and requires AVX2; there is no fallback
  for older CPUs.
- CPython 3.11–3.15 with the GIL. Free-threaded builds are not supported.
- The string scan can read up to 31 bytes past the end of a string, within the same memory
  page. This is safe, but Valgrind and AddressSanitizer report it.
- On import, yjson points the bundled Mojo runtime at the running interpreter. If
  `MOJO_PYTHON_LIBRARY` is already set, it takes precedence and must refer to that
  interpreter.

## Contributing

See [CONTRIBUTING.md](https://github.com/FarhanAliRaza/yjson/blob/main/CONTRIBUTING.md) for
building from source, running the tests and benchmarks, and making a release.

## Acknowledgements

- Float formatting and its power-of-ten table are ported from
  [Żmij](https://github.com/vitaut/zmij) by Victor Zverovich.
- The integer writer is ported from [itoap](https://github.com/Kogia-sima/itoap).
- The float parser and its power-of-five table follow
  [fast_float](https://github.com/fastfloat/fast_float) by Daniel Lemire and contributors.
- The API and the test suite in `tests/suite/` come from [orjson](https://github.com/ijl/orjson),
  whose source also informed the design.

Their licenses are reproduced in
[THIRD_PARTY_NOTICES.md](https://github.com/FarhanAliRaza/yjson/blob/main/THIRD_PARTY_NOTICES.md).

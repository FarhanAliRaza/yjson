# mojson

A JSON serializer for CPython written in Mojo, with an orjson-style API.
NaN / Infinity are written the way Python's `json` writes them
(`NaN`, `Infinity`, `-Infinity`), and NumPy arrays and scalars are supported directly.

## Build

Requirements: CPython 3.11, 3.12, 3.13, 3.14 or 3.15 (default GIL builds) with headers,
a C compiler, x86-64 with AVX2, and Mojo 1.1 (`pip install mojo`). The build
targets one interpreter at a time: it uses `.venv-bench/bin/python` if present,
otherwise `python3`; override with `PYTHON=...`.

```bash
./build.sh                               # -> build/mojson.cpython-312-x86_64-linux-gnu.so
PYTHON=python3.14 ./build.sh             # -> build/mojson.cpython-314-x86_64-linux-gnu.so
export PYTHONPATH="$PWD/build${PYTHONPATH:+:$PYTHONPATH}"
```

The output carries the interpreter's extension suffix, so builds for several
versions coexist in `build/` and each interpreter imports its own. The Mojo
loops read CPython object fields directly; `build.sh` probes those offsets from
the target headers (`src/layout_probe.c`) and passes them to both compilers.
The C shim re-checks them with `_Static_assert`, and the module verifies them
against live objects at import, so an unsupported interpreter fails with an
`ImportError` instead of reading memory wrongly. The layout differences in this
range are small: 3.11 has a longer str header (the legacy `wstr` field) and
keeps an int's sign and digit count in `ob_size`, for which the build selects a
tag-synthesizing variant that leaves the 3.12+ code untouched; 3.14 moved tuple
items by adding a cached tuple hash.

## Use

```python
import mojson
mojson.dumps({"a": [1, 2.5, None], "b": float("nan")})   # b'{"a":[1,2.5,null],"b":NaN}'
mojson.dumps(obj).decode()                                # if you need a str
mojson.loads(b'{"a": 1}')                                # -> {"a": 1}
```

See `examples/usage.py` (NumPy, NaN, files, errors).

`dumps(obj, /, default=None, option=None)` returns bytes. In addition to the
basic JSON types, it supports datetime/date/time, UUID, Enum, dataclass
instances, and subclasses of str/int/list/dict. Dataclass fields beginning
with `_` are omitted. Tuple subclasses use `default` rather than being treated
as arrays. Encoding errors are `JSONEncodeError`, an alias of `TypeError`;
exceptions raised by a default callback are attached as `__cause__`.

```python
import datetime
import decimal
import mojson

data = {"created": datetime.datetime(2024, 1, 2), "amount": decimal.Decimal("12.50")}
out = mojson.dumps(
    data,
    default=str,
    option=mojson.OPT_NAIVE_UTC | mojson.OPT_UTC_Z | mojson.OPT_INDENT_2,
)
mojson.dumps({2: "b", 1: "a"}, option=mojson.OPT_NON_STR_KEYS | mojson.OPT_SORT_KEYS)
mojson.dumps({"cached": mojson.Fragment(b'{"a":1}')})
```

Options use the same bit values as [orjson](https://github.com/ijl/orjson#option):
`OPT_INDENT_2`, `OPT_SORT_KEYS`, `OPT_NON_STR_KEYS`, `OPT_STRICT_INTEGER`,
`OPT_APPEND_NEWLINE`, `OPT_NAIVE_UTC`, `OPT_UTC_Z`, `OPT_OMIT_MICROSECONDS`,
and `OPT_PASSTHROUGH_DATACLASS`, `OPT_PASSTHROUGH_DATETIME`,
`OPT_PASSTHROUGH_SUBCLASS`. `OPT_SERIALIZE_NUMPY` is accepted; NumPy support
is already automatic. The deprecated `OPT_SERIALIZE_DATACLASS` and
`OPT_SERIALIZE_UUID` are zero. Flags can be combined with `|`.

Integers support the range `-2**63` through `2**64 - 1`; strict mode restricts
values to `-(2**53 - 1)` through `2**53 - 1`. Non-string integer keys retain
the 64-bit range in strict mode. Non-string key conversion preserves duplicate
JSON keys. Fragments insert their contents verbatim, including under indentation;
validate the contents yourself when needed.

`OPT_INDENT_2`, `OPT_SORT_KEYS` and `OPT_NON_STR_KEYS` run on the same
compiled writers as the compact default (indentation is a compile-time variant
of those loops; sorting and non-str keys snapshot the dict as native records),
so they stay close to orjson's speed for the same option. `OPT_STRICT_INTEGER`
and `OPT_PASSTHROUGH_SUBCLASS` use a generic traversal that checks every value
and is slower. Custom conversions and uncommon types cost additional work;
measure their speed on your payload (`bench/bench_shapes.py`). `loads` uses Python's standard parser with UTF-8, nonfinite-number,
and surrogate checks. It accepts str/bytes/bytearray/contiguous memoryview and
raises `JSONDecodeError` (a subclass of `json.JSONDecodeError`). Its parsing
speed and maximum nesting follow the stdlib backend, rather than orjson's parser.

`dumps_socket(obj, /, default=None)` is a separate native encoder for Reflex
wire packets. It accepts arbitrary-size integers (subject to CPython's decimal
digit limit), preserves NaN/Infinity and `None`, escapes lone surrogates, and
protects strings that collide with Reflex's special-value markers. It returns
bytes and accepts no formatting options. Framework custom types use `default`.
The integration in `bench/reflex_codec.py` replaces the socket boundary with
this function, eliminating stdlib retries and repeated Python container walks.
Ordinary `dumps()` keeps its integer range and UTF-8 error behavior.

## Test and benchmark

```bash
pip install orjson numpy
python tests/check_correctness.py path/to/jsonexamples     # corpus optional
python tests/check_features.py                            # options, types, callbacks, decoding
python bench/bench_paired.py path/to/jsonexamples          # mojson vs orjson, robust
python bench/bench_features.py                            # enabled feature paths vs orjson
python bench/bench_shapes.py --cpu 2                      # per payload shape vs orjson (where it wins or loses)
python bench/bench_regression.py                          # requires a baseline build in build/baseline/
python bench/bench_all_libraries.py path/to/jsonexamples   # + msgspec, ujson, rapidjson, json, simplejson
python bench/bench_numpy.py
```

Benchmark corpus: `jsonexamples/` from https://github.com/simdjson/simdjson-data.

The [Reflex PR 6116 comparison](bench/REFLEX.md) documents a pinned framework
checkout, unchanged upstream codec tests, paired encode/event benchmarks, and
real browser checks for both dump backends. Published measurements and validation
are in [bench/results](bench/results/README.md), including the general 14-file
comparison, native socket benchmarks and the default-path regression check.
The same directory holds the [CPython 3.11 - 3.15 measurements](bench/results/README.md#cpython-311---315-version-port)
and the [profile-driven improvements](bench/results/README.md#profile-driven-encoder-improvements-cpython-314)
measured on 3.14 (corpus 1.18× faster than before them; 1.28–1.32× of orjson on every interpreter).
The [all-events Reflex run](bench/results/README.md#reflex-pr-6116-every-event-workload)
measures every workload of the PR's event benchmark: all at parity, because
those deltas spend 88–97% of their encode time in Reflex's Python `default()`
serializer for pydantic models, which neither codec can skip.
The orjson baseline retains the PR's original
codec; mojson replaces its socket retry logic with native serialization.
Earlier option and indentation results remain in `build/fix-options.json`
and `build/indent-reflex-benchmark.json`.

### Local comparison with uv

If Mojo is already installed in `.venv`, keep that compiler environment and use
a separate CPython environment for the extension (3.12 shown; any of 3.11 - 3.15 works):

```bash
uv venv --python 3.12 .venv-bench
uv pip install --python .venv-bench/bin/python orjson numpy
PATH="$PWD/.venv/bin:$PATH" ./build.sh
.venv-bench/bin/python tools/fetch_corpus.py
.venv-bench/bin/python tests/check_correctness.py build/jsonexamples
.venv-bench/bin/python tests/check_features.py
.venv-bench/bin/python bench/bench_paired.py build/jsonexamples --cpu 2 --micro --output build/paired-local.json
```

To build and test every supported version side by side:

```bash
for v in 3.11 3.13 3.14 3.15; do
  uv venv --python $v .venv-py$v && uv pip install --python .venv-py$v/bin/python orjson numpy
  PATH="$PWD/.venv/bin:$PATH" PYTHON=.venv-py$v/bin/python ./build.sh
  .venv-py$v/bin/python tests/check_correctness.py build/jsonexamples
  .venv-py$v/bin/python tests/check_features.py
done
```

Choose an available logical CPU for `--cpu`, or omit it. The paired benchmark
checks output equality, warms both serializers, calibrates a common batch size
to at least 10 ms, and alternates order across 40 pairs. It reports median time
per call, the median `orjson time / mojson time` ratio, and ratio quartiles.
Ratios above 1 mean mojson is faster. The optional JSON report includes every
sample and environment metadata. The corpus downloader records the source
revision and SHA-256 hashes in `build/jsonexamples/manifest.json.txt`.

## What's inside (src/mojson.mojo)

- Direct reads of CPython object layouts (type pointer, list/tuple items, compact ints, float bits, str data)
  for 3.11 - 3.15, with the offsets supplied by the build and verified at import,
  and `external_call` into the CPython C API; output written straight into a `bytes` object.
- Floats: Żmij shortest round-trip core (one 64x128 multiply), SSE BCD digit conversion and `pshufb`
  decimal-point insertion (ported from zmij), exponent table, 4-float batches.
- Ints: itoap-style writer, and 4-at-a-time SIMD batches using zmij's 16-bit-lane digit trick.
- Strings: compact-ASCII / cached-UTF-8 access (as orjson), 64-byte SIMD escape scan with ctz jump,
  page-safe 32-byte masked tail.
- Dicts: entries are read straight from CPython's key table (no `PyDict_Next`
  call per key; split tables fall back to it), register-resident write cursor,
  per-call key cache (repeated key objects copy their escaped bytes), and inline
  `true`/`false`/`{}`/`[]` values. CPython's key-table kind keeps Unicode-only
  dictionaries on this writer even with `OPT_NON_STR_KEYS`. `OPT_SORT_KEYS` and
  non-str keys snapshot the entries as native records (stack storage up to 31
  entries), sorted with an inlined insertion/quicksort rather than `qsort`
  callbacks; int, float, bool and None keys are converted to text without
  creating Python strings.
- Lists: 4-wide SIMD batches for ints and floats, and short leaf lists of
  numbers (coordinate pairs) written in place without a nested call.
- NumPy: buffer-protocol fast path for contiguous float64/float32/int64/int32/uint8/bool, `.tolist()` fallback.

`tools/` holds the Python reference implementations used to validate the float core
(`zmij_reference.py`, `ref.py`) and zmij's power-of-ten table.

## Limitations

- NaN/Infinity output intentionally differs from orjson's `null`. The strict
  decoder rejects these tokens, so nonfinite output does not round-trip through `loads`.
- NumPy float32 values are formatted after promotion to float64, which can
  produce more decimal digits than orjson. Non-contiguous arrays use `.tolist()`.
- CPython 3.11 - 3.15 default (GIL) builds only: free-threaded (`t`) builds lay
  objects out differently and are rejected at build time. One build serves one
  minor version. x86-64 AVX2 by default; `MCPU=x86-64-v4 ./build.sh` builds
  an AVX-512 variant (k-mask string scanning) that only runs on such CPUs and
  was slower on a Cascade Lake Xeon (512-bit frequency penalty), so there is
  no runtime dispatch.
- Keep `build/_mojson_support.py` alongside the built `mojson.*.so`; the build copies
  this stdlib-only helper automatically. The extension has no orjson runtime dependency.
- The string tail reads up to 31 bytes past a string's end within the same memory page (safe, but
  AddressSanitizer/valgrind will flag it).
- A built `.so` needs the Mojo runtime libraries (`libKGENCompilerRTShared.so`, `libAsyncRTRuntimeGlobals.so`,
  `libMSupportGlobals.so`) available, e.g. from the `mojo` pip package; a wheel would have to bundle them.

## Credits

Float algorithm and power-of-ten table from Żmij by Victor Zverovich (https://github.com/vitaut/zmij, MIT);
integer writer structure from itoap; design informed by orjson's source (https://github.com/ijl/orjson).
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the upstream notices.

# Contributing to yjson

This guide covers building yjson from source, running the tests and benchmarks, and
publishing a release.

## Requirements

- x86-64 Linux with an AVX2 CPU
- CPython 3.11–3.15 (default GIL build) with its headers, and a C compiler
- [uv](https://docs.astral.sh/uv/)
- Mojo 1.1, pinned in the `mojo` dependency group

Install Mojo into its own environment and put it on `PATH`:

```bash
UV_PROJECT_ENVIRONMENT=.venv-mojo uv sync --only-group mojo --python 3.13
export PATH="$PWD/.venv-mojo/bin:$PATH"
```

## Building

`build.sh` compiles the extension for one interpreter: `$PYTHON` if set, otherwise
`.venv-bench/bin/python` if it exists, otherwise `python3`.

```bash
uv venv --python 3.13 .venv-bench
uv pip install --python .venv-bench/bin/python orjson numpy
./build.sh                     # -> build/yjson.cpython-313-x86_64-linux-gnu.so
PYTHON=python3.14 ./build.sh   # -> build/yjson.cpython-314-x86_64-linux-gnu.so
```

Each build carries its interpreter's extension suffix, so builds for several versions
coexist in `build/`. The tests, examples and benchmarks import from `build/` ahead of any
installed wheel; elsewhere, add `build/` to `PYTHONPATH`. The build copies the stdlib-only
helper `_yjson_support.py` next to the `.so`, and the two must stay together.

The Mojo writers read CPython object fields directly. `build.sh` probes their offsets from
the target interpreter's headers with `src/layout_probe.c` and passes them to both
compilers; the C shim re-checks them with `_Static_assert`, and the module verifies them
against live objects at import. Across 3.11–3.15 the layouts differ little: 3.11 has a
longer `str` header and stores an `int`'s sign and size in `ob_size` (handled by a
compile-time variant), and 3.14 added a cached hash to tuples.

| Variable | Effect |
| --- | --- |
| `PYTHON` | Interpreter to build for. |
| `MCPU` | Target CPU, `x86-64-v3` (AVX2) by default. `x86-64-v4` adds an AVX-512 string scan; it runs only on AVX-512 CPUs and was slower on a Cascade Lake Xeon, so it is opt-in. |
| `CC`, `MOJO` | C compiler and Mojo executables (`cc` and `mojo` by default). |
| `YJSON_DIRECT_DICT=0` | Iterate dicts with `PyDict_Next` instead of reading their key tables, for comparison. |

A `.so` built this way loads the Mojo runtime libraries (`libKGENCompilerRTShared.so` and
its dependencies) from the `mojo` package, through the RUNPATH recorded at link time.
Published wheels bundle them instead; see *Packaging*.

## Testing

```bash
python3 tools/fetch_corpus.py   # downloads the simdjson-data corpus to build/jsonexamples
.venv-bench/bin/python tests/check_features.py
.venv-bench/bin/python tests/check_correctness.py build/jsonexamples
.venv-bench/bin/python examples/usage.py
```

`check_features.py` checks options, types, callbacks and decoding against orjson.
`check_correctness.py` compares the output with orjson's byte for byte, on generated floats,
integers and strings and, when given, on the corpus. `check_loads.py` compares `loads` with
`orjson.loads` on the corpus, the suite's fixtures, generated numbers, strings and mutated
documents, and checks the float parser against `float()` on random doubles and on halfway
points between adjacent doubles:

```bash
.venv-bench/bin/python tests/check_loads.py build/jsonexamples
```

`tests/suite/` holds orjson 3.12.0's own test suite, run against yjson (see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)):

```bash
uv pip install --python .venv-bench/bin/python -r tests/suite/requirements.txt
.venv-bench/bin/python -m pytest tests/suite
```

Each test runs in a forked child, so a crash fails that test instead of ending the run. The
five intended differences (tests of `NaN`/`Infinity` output) are strict expected failures in
`tests/suite/conftest.py`: any other failure, or one of the five starting to pass, fails the
run.

To build and test every supported version:

```bash
for v in 3.11 3.12 3.13 3.14 3.15; do
  uv venv --python $v .venv-py$v
  uv pip install --python .venv-py$v/bin/python orjson numpy
  PYTHON=.venv-py$v/bin/python ./build.sh
  .venv-py$v/bin/python tests/check_correctness.py build/jsonexamples
  .venv-py$v/bin/python tests/check_features.py
done
```

## Benchmarks

Build first and fetch the corpus. Every script compares against orjson;
`bench_all_libraries.py` also needs msgspec, ujson, python-rapidjson and simplejson.

| Script | Measures |
| --- | --- |
| `bench/bench_paired.py CORPUS` | Each corpus document. The reference method. |
| `bench/bench_loads.py CORPUS` | `loads` on each corpus document; `--json` adds `json` and msgspec. |
| `bench/bench_shapes.py` | Synthetic payloads that each isolate one code path. |
| `bench/bench_features.py` | Options and feature paths. |
| `bench/bench_types.py` | Per-item cost of `datetime`, `UUID`, dataclasses, `Enum` and other types. |
| `bench/bench_numpy.py` | NumPy arrays, also against `json.dumps(a.tolist())`. |
| `bench/bench_all_libraries.py CORPUS` | msgspec, ujson, python-rapidjson, `json` and simplejson as well. |
| `bench/bench_regression.py` | The current build against a baseline build in `build/baseline/`. |

`bench_paired.py` checks that both libraries produce the same output, warms them up,
calibrates a common batch size of at least 10 ms, and alternates their order across 40
pairs. It reports the median time per call and the median and quartiles of the orjson/yjson
time ratio; values above 1 mean yjson is faster. `--cpu N` pins the process to one logical
CPU, `--micro` adds small payloads, and `--output FILE` saves every sample with environment
metadata. The corpus downloader records the source revision and SHA-256 hashes in
`build/jsonexamples/manifest.json.txt`.

```bash
.venv-bench/bin/python bench/bench_paired.py build/jsonexamples --cpu 2 --micro \
    --output build/paired.json
```

Recorded results, with the hardware and method behind them, are in
[`bench/results/README.md`](bench/results/README.md).

## Packaging

`pyproject.toml` holds the package metadata. `setup.py` only tells setuptools to compile the
extension through `build.sh`, so `uv build` and `pip install .` work with Mojo on `PATH`.
The `test`, `wheel` and `mojo` dependency groups are pinned in `uv.lock`.

```bash
uv build --sdist
uv build --wheel --python 3.13 --out-dir dist dist/yjson-*.tar.gz
UV_PROJECT_ENVIRONMENT=.venv-tools uv sync --only-group wheel   # auditwheel, patchelf
MOJO_LIB=$(.venv-mojo/bin/python -c 'import modular; print(modular.__path__[0] + "/lib")')
PATH="$PWD/.venv-tools/bin:$PATH" auditwheel repair --ldpaths "$MOJO_LIB" \
    --disable-isa-ext-check --wheel-dir wheelhouse dist/*.whl
```

`auditwheel repair` copies `libKGENCompilerRTShared.so` and its dependencies into
`yjson.libs/` and sets the `manylinux_2_35` tag, the glibc floor of the Mojo runtime.
`--disable-isa-ext-check` is required because the extension targets x86-64-v3 by design.

## Continuous integration

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push to `main` and on
every pull request. It builds the sdist, builds and repairs one wheel per CPython version
from that sdist, installs each wheel into a clean environment, and runs
`check_features.py`, `check_correctness.py` with the corpus, `examples/usage.py` and
orjson's test suite against it. A separate job benchmarks against orjson on CPython 3.13
and writes the tables to the run summary; shared runners are noisy, so it never fails the
build.

## Releasing

1. Bump `version` in `pyproject.toml` and commit.
2. Tag the commit and push the tag:

   ```bash
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

When the tag matches the version in `pyproject.toml`, CI publishes the sdist and wheels to
PyPI through [trusted publishing](https://docs.pypi.org/trusted-publishers/) from the
`pypi` GitHub environment, so no API token is stored.

## Project layout

| Path | Contents |
| --- | --- |
| `src/yjson.mojo` | The encoder: type dispatch, the writers, and module initialization. |
| `src/python_api.c` | C shim: entry points, layout checks, and conversions of less common types. |
| `src/decoder.c` | The `loads` parser, independent of the encoder. `src/decoder_powers.h` is its power-of-five table, generated by `tools/powers_of_five.py`. |
| `src/_yjson_support.py` | Stdlib-only helper: option constants, `Fragment`, `JSONDecodeError` and fallback conversions. |
| `src/layout_probe.c` | Prints the CPython object offsets that `build.sh` passes to both compilers. |
| `src/yjson.pyi` | Type stubs. |
| `tests/` | Feature and correctness checks, and orjson's test suite in `tests/suite/`. |
| `bench/` | Benchmarks, with recorded results in `bench/results/`. |
| `tools/` | Corpus downloader, the Python reference implementations and power-of-ten table used to validate the float core, and the generator of the parser's power-of-five table. |

## Implementation notes

- **Object access.** The writers read CPython object layouts directly (type pointers, list
  and tuple items, compact ints, float bits, str data), call the C API through
  `external_call` only where needed, and write straight into a `bytes` object.
- **Floats.** Żmij's shortest round-trip core (one 64×128-bit multiplication), SSE BCD digit
  conversion and `pshufb` decimal-point insertion, and 4-float batches. `float32` and
  `float16` use Żmij's binary32 variant.
- **Integers.** An itoap-style writer, and 4-at-a-time SIMD batches using Żmij's
  16-bit-lane digit trick.
- **Strings.** Compact-ASCII and cached UTF-8 access, as in orjson, and a 64-byte SIMD
  escape scan with a page-safe 32-byte masked tail.
- **Dicts.** Entries are read straight from the key table (split tables fall back to
  `PyDict_Next`), repeated key objects reuse their escaped bytes, and `true`, `false`, `{}`
  and `[]` values are written inline. Sorted and non-`str` keys are snapshotted as native
  records and sorted with an inlined insertion sort and quicksort; `int`, `float`, `bool` and
  `None` keys become text without creating Python strings.
- **Lists.** 4-wide SIMD batches for ints and floats. Short leaf lists of scalars, such as
  coordinate pairs, are written in place without a nested call.
- **Other types.** `date`, `time`, naive and UTC `datetime` values, `UUID`s, and int and str
  subclasses such as `IntEnum` are written from their object layouts. Other time zones get
  their offset through the C shim, `Enum` members are read from `_value_`, dataclass
  instances from their `__dict__` when they have one, and NumPy arrays from their buffers.
- **Options.** `OPT_INDENT_2` is a compile-time variant of the compact writers, and sorting
  and non-`str` keys use the native records above. `OPT_STRICT_INTEGER` and
  `OPT_PASSTHROUGH_SUBCLASS` use a generic traversal that checks every value.

The measurements behind these choices are recorded in
[`bench/results/README.md`](bench/results/README.md).

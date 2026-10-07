# Mojo decoding and the C benchmark baseline

`src/decoder.mojo` is the package's default parser for both untyped `loads` and
`loads(type=...)`. `src/decoder_api.c` supplies the CPython binding, annotation
plans and Python object hooks. The former C parser lives in `bench/c_decoder/`
and is built separately; it is not linked into the package.

`compare.py` measures shipping `yjson.loads` against `_c_decoder.loads` and
orjson. The optional standalone `_mojo_decoder` adapter builds the same production
Mojo source for profiling and context cleanup checks. `--ctypes` calls the
shipping parser through ctypes to measure Python boundary overhead.

The latest production measurements, including all encoding and decoding tables,
are recorded in [bench/results/mojo-default](../results/mojo-default/README.md).

## What changed

- Numeric arrays pass the cursor directly to an inlined number parser and keep its
  returned cursor local instead of storing and reloading `Parser.cur` for every value.
- Array fractions consume eight digits with one validated word load when enough
  input remains. Long mantissas still fall back to the original conversion path.
- A compile-time parameter preserves the compact original scalar/object number
  path, avoiding the extra bulk-digit check for short object-field numbers.
- Whitespace runs use explicit ordinary-space and non-whitespace branches for the
  scalar prefix and tail, keeping the existing 16-byte SIMD scan for longer runs.
- Integers return immediately after the digit chain. General float conversion is
  kept out of line so integer and Clinger paths do not execute Eisel-Lemire checks
  or carry its register demand. Subnormal rounding is also kept out of line.
- Lists with one to four items copy their references directly; two overlapping
  16-byte loads/stores cover lengths two to four without reading outside either
  buffer. Larger lists retain the bulk copy.
- Cached ASCII keys of 8–16 bytes compare bounded first/last words directly;
  other lengths retain `memcmp`.
- `native.c` exposes `_mojo_decoder.loads` as a native Python method with the GIL
  held. The old ctypes wrapper remains available for measuring boundary overhead.
- Each native module owns its decoder context and frees cached keys and buffers on
  teardown. The build probes the selected interpreter's CPython object layout.

The numeric changes bring the port up to date with C optimizations added in
`27530ac`. Earlier 6–9% slowdown measurements came from a shared Xeon and the older
parser through ctypes; they were not a controlled measure of a compiler penalty.

## Optimization measurements before promotion

Measured on a Ryzen 5 5600, CPython 3.13.14, Mojo 1.1.0 (`8189361e`, x86-64-v3),
and orjson 3.12.0. Native entry points are used for both Mojo versions and C, with
40 paired batches of at least 10 ms, rotating measurement order on logical CPU 5
and `PYTHONHASHSEED=0`. The summaries are geometric means over the 14 original
corpus files and their 14 compact variants, separately. The additional records
payload is excluded from both summaries. These are local measurements, not a
universal speed guarantee.

In `build/local-mojo-residual-benchmark.json`, every one of the **29 measured
inputs has a paired median Mojo/C ratio below 1**. The slowest relative result is
marine_ik.json at 0.979× C time. The original corpus summary is **0.943× C time**
(5.7% less time), and the compact corpus is **0.939×** (6.1% less time).
orjson/Mojo time is **1.217×** on the original corpus. Compared with the saved
Mojo version after the whitespace fix, time fell 2.8% on the original corpus
and 2.9% on compact variants.

The fresh-process repeat (`build/local-mojo-residual-benchmark-repeat.json`) also
has **zero inputs with a paired median slower than C**. Its original corpus
Mojo/C time is 0.948× and compact time is 0.940×; its slowest relative result is
the records payload at 0.984×. Both runs used the same binary, affinity, hash seed,
and batch settings. Across the two runs, original-corpus time was 5.2–5.7% below C
and compact-corpus time was 6.0–6.1% below C. Individual batches still overlap
parity on some inputs; this result applies to the measured medians on this machine.

| Former laggard | C median (µs) | Mojo median (µs) | Paired Mojo/C time |
| --- | ---: | ---: | ---: |
| canada.json | 4222.2 | 4005.5 | 0.948× |
| instruments.json | 254.7 | 240.4 | 0.946× |
| instruments.json (compact) | 214.7 | 201.8 | 0.939× |
| marine_ik.json | 5810.4 | 5679.6 | 0.979× |
| marine_ik.json (compact) | 5751.4 | 5530.3 | 0.965× |
| citm_catalog.json (compact) | 1500.3 | 1446.6 | 0.963× |

Pairwise ratios and ratios of independent medians can differ slightly. Results are
not uniform gains against the prior Mojo build: apache_builds.json takes 1.2% more
time and gsoc-2018.json 1.3% more in this run, while still ahead of C. Compact
variants remove only whitespace outside strings, preserving number spelling and
quoted content. Code layout, input alignment, allocator state, and other active
processes can affect timings; saved paired samples include the quartile ranges.

The earlier numeric-only run (`build/local-mojo-final-benchmark.json`) put Mojo
approximately level with C (1.000× C time) and measured 7.7% less time on
numbers.json and 4.0% less on mesh.json against the original native port. The
subsequent whitespace run (`build/local-mojo-whitespace-benchmark.json`) measured
0.963× C time across the original corpus; its baseline preceded the whitespace
change. The current baseline already includes both numeric-array and whitespace
optimizations.

Profiles verify the executed-work changes independently of wall-time noise:

- The whitespace fix reduced instruments.json scanner instructions by 39.4%
  (17,736,100 → 10,753,225) across 25 collected calls.
- The later integer/float changes reduced compact-instruments number-parser
  instructions by 41.4% (8,943,625 → 5,242,375). Dictionary-insertion counts stayed
  unchanged, and total instructions fell 6.1% with all retained changes.
- Direct cached-key comparisons reduced compact-citm `memcmp` instructions by
  47.3% against the preceding numeric/list candidate. Total instructions fell 1.1%.
- Total marine_ik.json instructions fell 4.4% against the post-whitespace baseline.

Counts do not establish the cause of every timing difference. Profiles and
candidate samples are in `build/mojo-residual-profile/`.

Candidates tested and dropped:

- An earlier rewrite of the general whitespace classifier produced bit-test
  assembly but cost about 1.2% overall. The retained rewrite changes only scalar
  checks within whitespace runs. A subsequent eight-space word shortcut made
  the formatted screening cases 3.5% slower than the retained version and was
  also dropped.
- A 16-bit prediction table saved 12 KiB per context, but did not consistently
  improve speed and slowed the records payload in the follow-up comparison.
- Allowing the number parser to remain out of line shrank the array function but
  made `numbers.json` about 13% slower and `mesh.json` about 8% slower than the
  inlined version in the paired screening run.
- Scalar short-list copies with a conditional store per item did not improve the
  screening average. The retained version uses two bounded vector stores.
- An out-of-line rounding-tie helper improved Canada but slowed the screening
  average by about 0.7% and was dropped.

## Correctness

The optimized parser passed the untyped differential checker: **502,254 checks, zero differences**, including 419 corpus/fixture
documents, generated numbers and float rounding, escaped strings, invalid UTF-8,
mutated inputs, and bytes/str/bytearray/memoryview variants. It also passes
**354 upstream parsing/JSONChecker tests** with the Mojo loader substituted, plus
ten regression tests covering fractional boundaries, mixed numeric arrays, errors
and subsequent calls, input variants, and context teardown. Whitespace tests cover
all 256 byte values and runs around scalar and SIMD boundaries with spaces, tabs,
carriage returns, and newlines. Float tests compare exact IEEE-754 bits across the
Clinger/Eisel-Lemire boundary, underflow, subnormal rounding, and overflow. Nested
list tests check item ownership and stack offsets across direct and bulk copies.
Cached-key tests vary lengths, key order, and prefix/middle/suffix mismatches.

The standalone native adapter deliberately exposes only untyped `loads(input)`. Its errors
are `ValueError` with a byte position, not the package's full `JSONDecodeError`
API. Shipping `yjson.loads` preserves the full `JSONDecodeError` API and supports
`type=...`; typed traversal is implemented in Mojo.

## Running it

From the repository root, with the Mojo compiler and benchmark dependencies
installed (see `CONTRIBUTING.md`):

```bash
MOJO="$PWD/.venv-mojo/bin/mojo" ./build.sh
bench/c_decoder/build.sh
PYTHON="$PWD/.venv-bench/bin/python" MOJO="$PWD/.venv-mojo/bin/mojo" bench/mojo_decoder/build.sh
.venv-bench/bin/python tests/check_loads.py build/jsonexamples
.venv-bench/bin/python tests/check_typed_loads.py
.venv-bench/bin/python bench/mojo_decoder/check.py build/jsonexamples
PYTHONPATH=bench/mojo_decoder:build .venv-bench/bin/python bench/mojo_decoder/test_decoder.py
PYTHONHASHSEED=0 .venv-bench/bin/python bench/mojo_decoder/compare.py build/jsonexamples \
  --cpu 5 --pairs 40 --compact --ctypes --output build/mojo-benchmark.json
```

Select an available logical CPU for `--cpu`. `build.sh` defaults to `.venv-bench`
and then `python3` if `PYTHON` is omitted; `MOJO` defaults to `mojo` on PATH. It
supports GIL builds of CPython 3.11–3.15. `loads.so` is built for one interpreter
layout at a time, so rebuild when switching Python versions.

To run upstream parsing tests against the shipping parser:

```bash
.venv-bench/bin/python -m pytest tests/suite/test_parsing.py tests/suite/test_jsonchecker.py -q
```

`compare.py --baseline /path/to/saved_native_extension.so` additionally measures
a saved Mojo implementation. Its parser library must have a distinct SONAME so
the dynamic loader does not reuse the current library. All benchmarked decoders
are checked against orjson on the documents before timing; the current Mojo
parser is also checked on malformed and numeric edge cases. JSON output includes
environment information, affinity, per-document medians, and every batch sample.
`--compact` also measures all corpus documents with whitespace outside strings
removed; number spelling and quoted content are preserved, and the decoded values
are checked against the originals. Original and compact corpus summaries are
reported separately; the additional records payload is excluded from both.

`src/decoder_powers.c` exposes the generated `src/decoder_powers.h` table
through an accessor. Compiler-source findings and historical profiles are in
[the decoder notes](../../docs/decoder-notes.md); neither instruction counts nor
source inspection alone establish a net compiler slowdown.

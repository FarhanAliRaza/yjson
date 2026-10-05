# Reflex PR 6116 comparison

The checkout in `build/reflex-pr6116` is a GitHub source archive pinned to
`f2b00b407b892778e259eb0c671a89c03524ea7d`. The existing Reflex checkouts were
left untouched. `uv sync --frozen --python 3.12 --extra orjson` installed the
PR's locked dependencies. Because the archive lacks Git tags and its configured
fallback version is invalid, installation used
`UV_DYNAMIC_VERSIONING_BYPASS=0.9.99` for workspace package metadata only.

Both encoders run on CPython 3.12.13, with orjson 3.12.0 and the same Reflex
source. The JSON reports include the mojson binary hash. This is a dumps-only
experiment: Socket.IO input still uses the PR's stdlib decoder, preserving
arbitrary-precision integers. Framework utility loads still use orjson.

`reflex_codec.py` uses `mojson.dumps` for artifacts and replaces the socket
boundary with `mojson.dumps_socket`. Both the base module and the compatibility
re-export are updated, because the app calls the latter. The mojson socket path
has no stdlib retry or Python collision walk. The orjson baseline keeps the
original PR codec. Constants, utility loads, and incoming Socket.IO decoding
remain unchanged. Nothing changes the globally imported orjson module.

From the mojson repository root, reproduce the checks and timings:

```bash
export PYTHONPATH="$PWD/build:$PWD/bench:$PWD/build/reflex-pr6116"
export REFLEX_DIR=/tmp/mojson-reflex-data
cd build/reflex-pr6116
uv run --no-sync --python 3.12 pytest tests/units/utils/test_format_orjson.py -q
uv run --no-sync --python 3.12 pytest -p reflex_pytest tests/units/utils/test_format_orjson.py -q
uv run --no-sync --python 3.12 pytest -p reflex_pytest tests/units/test_app.py -k 'sio_codec or socket_codec_preserves_large_ints or sio_json_records_message_sizes or custom_sio or stateful_pages_marker' -q
uv run --no-sync --python 3.12 python ../../bench/check_reflex_compat.py
uv run --no-sync --python 3.12 python ../../bench/check_reflex_native.py
uv run --no-sync --python 3.12 python ../../bench/bench_reflex.py --batch-ms 25 --output ../socket-reflex-benchmark.json
uv run --no-sync --python 3.12 python ../../bench/bench_reflex.py --options-only --output ../fix-options.json
```

Final timing uses 40 alternating paired batches of at least 25 ms, pinned to logical
CPU 2, with GC enabled. Ratios are the median of each pair's orjson/mojson
times; values above 1 mean mojson is faster. Quartiles describe spread, not
confidence intervals. Absolute times cannot be compared to the PR description's
different CPU/Python measurements. The large fixture here is approximately
95.8 KB per wire delta, with three deltas per measured event batch.

## Native option-path fix

The following option and indentation sections record earlier builds that kept
the PR's socket retry logic. The current native socket integration is described
in the final section.

Native `py-spy` sampling of identical 500-row payloads with the PR's socket flags
found `mojson_prepare_items` on approximately 40% of sampled stacks before the
fix. `OPT_NON_STR_KEYS` was building Python item tuples and routing ordinary
string dictionaries through the configurable walker. The matching orjson 3.12.0
source, `src/serialize/writer/container.rs`, uses native vectors for its non-string
and sorted paths. Its non-string path also copies string keys into owned strings.
The official source archive is retained in `build/orjson-source/`.

Mojson now specializes compact traversal for the non-string and sorting flags.
CPython 3.12's Unicode/split dictionary table kind guarantees exact string keys,
allowing these dictionaries to use the cached direct writer with no temporary
item pairs. General key tables retain the conversion fallback, including tables
that once held non-string keys. Nested containers and callback results preserve
the flags. Sorting collects native UTF-8 key/value records, using a 32-entry stack
buffer and insertion sort up to 16 entries; larger dictionaries use heap storage
and `qsort`. Owned references keep sorted snapshots safe across callbacks.

The rebuilt socket-flags profile contains no samples in item preparation or the
configurable walker. Raw profiles and binary hashes are in `build/profile-before.txt`,
`build/profile-after.txt`, and `build/fix-profile-summary.json`. Profiling and timing
runs are separate. Default traversal remains a separate compiled variant.

For the initial non-string/sorting fix, the paired median ratios to orjson were:

| Workload | Before | After [quartiles] |
| --- | ---: | ---: |
| Native 500-row payload with socket flags | 0.84× | 2.22× [2.04–2.34] |
| Reflex socket encode, 5 rows | 0.88× | 1.11× [1.08–1.12] |
| Reflex socket encode, 500 rows | 0.88× | 1.45× [1.38–1.51] |
| Reflex socket encode with nulls, 500 rows | 0.95× | 1.10× [1.06–1.14] |
| Reflex sorted artifact | 0.57× | 1.01× [0.93–1.10] |
| Reflex indented artifact | 0.92× | 0.92× [0.89–0.99] |
| Reflex event processing, 3 events, 500 rows | 0.93× | 1.06× [0.97–1.13] |

The full event-processing improvement has spread that includes parity; it is not
a measured 2.22× application speedup. That build still had the indentation loss
addressed below. The PR's null/sentinel checks, serializer callbacks and fallback passes
still contribute to application time. See `build/fix-reflex-benchmark.json` and
`build/fix-options.json` for all samples, including special-value and small-event
cases. Pre-fix reports remain in `build/reflex-benchmark.json` and
`build/reflex-options.json`.

Validation: 16 feature-test groups, all 156 selected upstream Reflex tests,
and the float/string/container/NumPy fuzz and 14-file corpus checks pass.
New cases cover Unicode sorting, stack/heap boundaries, mixed-key and split
dictionaries, callback mutation, error cleanup, and reference ownership.
`build/fix-regression.json` compares ordinary `dumps(obj)` against the saved
pre-fix binary using alternating pairs; quartiles must be considered alongside
the median. The 14-file geometric mean is 1.002× against the previous binary.
Tiny results vary: the small dictionary is 104 ns before and 106 ns after
(0.978× [0.963–0.984]); a short string improves from 73 ns to 68 ns. The
aggregate result does not guarantee every tiny call is unchanged. Reproduce
this check from the mojson root:

```bash
uv run --python 3.12 --no-project python bench/bench_regression.py \
  --baseline build/reflex-before/mojson.so --batch-ms 25 \
  --output build/fix-regression.json
```

The event benchmark uses the PR's real `BaseStateEventProcessor`,
`StateManagerMemory`, generated `WireBenchState` events, serializer registry,
and actual `_sio_dumps`/`_sio_loads`. It measures decode/process/encode without
network transport or browser rendering. Encode cases and emitted wire packets
are checked byte-for-byte before timing. Additional semantic probes found two
byte differences when a nonfinite float shares a payload with a small finite
float: orjson's PR fallback emits `1e-07`, while mojson emits `1e-7`; decoded
values agree. See `build/reflex-compat.json`.

## Indentation profile and fix

The previous indented writer had a separate call to `indent_line` for each
newline and indentation, followed by a `memset` even for two, four, or six spaces.
Native sampling put approximately 12% of leaf samples in `indent_line` and 37%
in `ser_configured`. Unlike compact traversal, every primitive child went
through that dispatcher. Orjson's official `byteswriter.rs` source inlines its
indentation formatter. Its shipped binary has stripped frames, so the source
comparison is more precise than assigning names to every orjson sample.

The shallow indentation path now reserves eight bytes and writes a newline and
spaces with one inline store, advancing by the exact logical length. Deeper
indentation retains the general fill. Primitive children are written inline in
indented containers, preserving integer-limit checks and the existing fallback
for other types. Indented direct dictionaries also reuse the existing escaped-key
cache, with generation checks around callbacks. The default compact traversal
is unchanged. `ser_configured` falls to approximately 7% of leaf samples in the
new profile; there is no separate `indent_line` frame after inlining. Sampling
fractions and profiler call counts are not benchmark speedups.

An isolated 40-pair indented Reflex run measured 1.02× [1.01–1.03] against orjson.
The complete 25 ms/batch rerun measured 1.01× [1.01–1.03], with median times
124 µs for mojson and 126 µs for orjson. This closes the previous loss; the
advantage is small. Socket encode remains 1.13× for 5 rows and 1.43× for 500
rows; sorted artifacts are 1.02× [1.00–1.03]. Large three-event processing is
1.06× [1.04–1.12]. Special-value packets are still slightly slower at 0.97×;
they include exceptions and the PR's stdlib fallback. Small three-event
processing is tied at 0.99× [0.98–1.01].

Reports: `build/indent-reflex-benchmark.json`, `build/indent-step3.json`,
`build/indent-profile-summary.json`, and raw `build/profile-indent-*.txt`.
Binary hashes identify each version. The default 14-file/tiny-payload comparison
is in `build/indent-regression.json`, using the prior binary saved under
`build/indent-before/`: its geometric mean is 1.002×, effectively unchanged.
The small dictionary measures 104 ns in both builds, with paired ratios
0.99× [0.98–1.02]. Run the focused benchmark with `--case 'artifact indent'`
to avoid measuring the other cases during development.

All 17 feature tests, 156 selected Reflex tests, and the full correctness/fuzz
checks pass. New cases cover shallow/deep indentation, output growth boundaries,
short/long/escaped repeated keys, strict integers, and reentrant callbacks.

The real app uses isolated frontend/backend pairs:

- mojson: `examples/pr6116_repro`, http://localhost:3036/ (backend 8036).
- orjson: `examples/pr6116_orjson`, http://localhost:3037/ (backend 8037).

Start the mojson app after the environment is installed:

```bash
cd examples/pr6116_repro
REFLEX_DUMP_BACKEND=mojson uv run --no-sync --python 3.12 python ../../../../bench/reflex_cli.py run
```

The CLI wrapper installs the encoder before compilation; the app installs it
again when imported by backend workers. The orjson app uses the ordinary
`uv run --no-sync --python 3.12 reflex run` command. Avoid importing mojson
from `sitecustomize`: its runtime probes Python in child interpreters, which
would inherit that startup hook and recurse.

Browser checks exercise counters before and after unusual values, two 500-row
refreshes, direct frontend reads of row values, NaN/Infinity/null, sentinel
collisions, escaped Unicode text, and a form echo. The form submits one complete
string; a raw controlled input with per-keystroke server updates introduced a
typing race in the first example. Final assertions and console evidence are in
`build/indent-reflex-browser.json` for the rebuilt library (the earlier checks are in
`build/reflex-browser.json`). Production export also passes for the rebuilt
mojson app. Both servers are left running for inspection.

## Native socket serialization

The special-value profile spent approximately 83% of steady-call samples in
the PR's stdlib socket fallback for both encoders. It included `2**100`, causing
a native integer rejection before the stdlib serialized the packet, scanned
for markers, walked containers, and serialized it again. `None` also triggered
the PR's retry because orjson represents both nonfinite floats and None as null.

Mojson now has a separate `dumps_socket` entry point and a compile-time socket
variant of its compact traversal. It preserves None and bare nonfinite tokens,
writes arbitrary-size integers through CPython's decimal integer formatter,
and escapes lone surrogates in a native string helper. This uses no stdlib JSON
encoding. Ordinary `dumps` retains its 64-bit integer limits and UTF-8 errors.
The socket-specific code is removed from its compiled traversal.

Marker-bearing output is scanned and rewritten in C. User string values that
collide with Reflex sentinels get one escape prefix; keys remain untouched.
Nonfinite tokens are sentinelized when the packet contains the marker prefix,
matching the PR's wire behavior. Marker-free output returns the same bytes
object. Custom serializer results are processed within that output too, so
there is no recursive Python marker walk and no second serialization.

The native socket converter routes Enum and UUID through Reflex's callback
when one is supplied, preserving registry overrides. Callback parent ownership
is retained, including a single-parent fast path. Enormous integer conversion
can import `_pylong` with the digit limit disabled, so the native path pins the
active parents and integer around that call as well. Tests exercise an import
hook that clears the parent list and runs GC during conversion.

`check_reflex_native.py` compares 3,000 fuzzed real StateUpdate packets against
the unchanged PR orjson output by decoded value. During the native run it makes
`json.dumps`, `_json_dumps_socket_fallback`, and `_replace_non_finite_floats`
raise immediately. All packets pass. The additional 24 feature tests and 156
selected upstream Reflex tests pass, and the full corpus/fuzz checks report
zero mismatches for ordinary dumps. Small finite floats can have different
decimal spellings from the PR's stdlib output; decoded values agree.

Current reports are `build/socket-reflex-benchmark.json`,
`build/socket-regression.json`, `build/socket-native-validation.json`,
`build/socket-profile-summary.json`, and `build/socket-reflex-validation.json`.
The default-path baseline is saved in `build/socket-before/`.

Published copies of the current reports are in [results/](results/README.md).
The `build/` paths above and earlier in this document refer to local generated
artifacts; historical reports and framework checkouts are not committed.

The final 40-pair rerun (median ratios, quartiles in brackets) measured:

| Workload | orjson µs | mojson µs | Ratio [quartiles] |
| --- | ---: | ---: | ---: |
| socket encode / 5 rows | 3.59 | 2.18 | 1.64× [1.60–1.69] |
| socket encode / 500 rows | 180.62 | 51.12 | 3.54× [3.50–3.60] |
| socket None / 500 rows | 588.97 | 50.31 | 11.65× [11.50–12.05] |
| socket special values | 14.70 | 2.27 | 6.52× [6.36–6.65] |
| artifact compact | 103.89 | 90.64 | 1.14× [1.11–1.18] |
| artifact indent | 130.31 | 131.86 | 0.99× [0.98–1.01] |
| artifact sorted | 126.97 | 129.72 | 0.98× [0.97–0.99] |
| wire 3 events / 5 rows | 359.15 | 356.66 | 1.01× [0.98–1.07] |
| wire 3 events / 500 rows | 2398.42 | 2014.87 | 1.18× [1.17–1.20] |

The socket gains include removing framework retry overhead, rather than just
changing the encoder implementation. Full event-processing results include
decode and state processing, and exclude network/rendering. Sorted artifacts
remain about 2% slower in this run; indentation is within a percent of parity.

The default 14-file geometric mean is 1.001× against the
saved preceding build. Tiny calls vary by a few nanoseconds: the small dict
is 104 ns before and 107 ns after; the empty dict is 72 ns and 73 ns. Float
is 85 ns in both builds. This is aggregate parity, not a guarantee that every
payload has identical timing.

The native steady-call profile contains zero samples in stdlib JSON, the
retry function, or the Python marker walk. The previous packet profile had
83% of call samples in the retry. Necessary custom-type callback conversion
remains in the native profile. Its internal `ser_fallback` name denotes type
conversion; it does not call a second JSON encoder.

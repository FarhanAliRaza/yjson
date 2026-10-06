# Local benchmark evidence

Measured on AMD Ryzen 5 5600 (AVX2), CPython 3.12.13, Mojo 1.1.0 and
orjson 3.12.0. The extension hash is recorded in the reports.
Absolute project paths have been normalized to relative paths in these copies;
timing samples and hashes are preserved. Generated binaries and historical
framework checkouts are excluded.

## General serialization

[Full report](general-benchmark.json) / [text summary](general-benchmark.txt).
40 alternating paired runs, a common batch calibrated to at least 25 ms, CPU 2,
and GC disabled during timing. Output is byte-identical on all 14 corpus files.
The corpus geometric mean is **1.307×** relative to orjson. Ratios above 1
mean mojson is faster. Quartiles describe sample spread, not confidence intervals.

| Case | Median ratio | Middle half |
| --- | ---: | ---: |
| apache_builds | 1.35× | 1.31–1.38 |
| canada | 1.00× | 0.97–1.02 |
| citm_catalog | 1.01× | 0.99–1.04 |
| github_events | 1.33× | 1.29–1.39 |
| gsoc-2018 | 1.30× | 1.27–1.33 |
| instruments | 1.34× | 1.31–1.36 |
| marine_ik | 1.25× | 1.23–1.27 |
| mesh | 1.35× | 1.34–1.37 |
| mesh.pretty | 1.36× | 1.34–1.38 |
| numbers | 1.50× | 1.47–1.52 |
| random | 1.38× | 1.36–1.40 |
| twitter | 1.35× | 1.34–1.38 |
| twitterescaped | 1.38× | 1.36–1.41 |
| update-center | 1.51× | 1.44–1.55 |
| small dict | 1.15× | 1.13–1.16 |
| empty dict | 1.00× | 0.98–1.02 |
| int | 1.07× | 1.04–1.08 |
| float | 1.12× | 1.09–1.14 |
| short string | 1.14× | 1.12–1.19 |
| mixed list | 1.18× | 1.15–1.20 |

To reproduce after building:

```bash
.venv-bench/bin/python tools/fetch_corpus.py
.venv-bench/bin/python bench/bench_paired.py build/jsonexamples --cpu 2 --pairs 40 --batch-ms 25 --micro --output build/general-benchmark.json
```

## CPython 3.11 - 3.15 (version port)

[Reports](python-versions/): `regression-3.12.*` and `paired-3.1x.*`.
Supported range: 3.11 through 3.15. 3.10 reached end of life in October 2026
and was not ported.
Measured in a shared cloud container (Intel(R) Xeon(R) Processor @ 2.80GHz, AVX2 build, CPU 2 pinned,
40 alternating pairs, batches of at least 20 ms), Mojo 1.1.0, orjson 3.12.0 on
every interpreter: CPython 3.11.15, 3.12.3, 3.13.14, 3.14.8 and 3.15.0rc3. The container
is slower and noisier than the desktop runs above, so compare columns here with
each other rather than with the table above.

`regression-3.12` loads the build from before the version port (commit
`0f0944d`) and the current build in one process on CPython 3.12: corpus
geometric mean **1.170×** (current / original). The port itself changed nothing
(measured 1.002× before the performance work below, with identical 3.12 machine
code in the hot paths); the gain comes from the encoder improvements. The
encoder's absolute times are flat across interpreters: the hot paths differ only
by compile-time constants, and the ratios below move with orjson's own speed
on each interpreter.

| Case | 3.11 | 3.12 | 3.13 | 3.14 | 3.15 |
| --- | ---: | ---: | ---: | ---: | ---: |
| apache_builds | 1.22× | 1.28× | 1.20× | 1.27× | 1.29× |
| canada | 1.07× | 1.14× | 1.08× | 1.05× | 1.10× |
| citm_catalog | 1.19× | 1.22× | 1.23× | 1.27× | 1.27× |
| github_events | 1.43× | 1.33× | 1.35× | 1.33× | 1.32× |
| gsoc-2018 | 1.12× | 1.08× | 1.08× | 1.05× | 1.00× |
| instruments | 1.71× | 1.41× | 1.42× | 1.50× | 1.45× |
| marine_ik | 1.16× | 1.20× | 1.21× | 1.22× | 1.22× |
| mesh | 1.15× | 1.21× | 1.24× | 1.25× | 1.29× |
| mesh.pretty | 1.19× | 1.26× | 1.30× | 1.31× | 1.31× |
| numbers | 1.28× | 1.42× | 1.49× | 1.50× | 1.51× |
| random | 1.36× | 1.36× | 1.36× | 1.39× | 1.39× |
| twitter | 1.37× | 1.33× | 1.32× | 1.35× | 1.33× |
| twitterescaped | 1.37× | 1.28× | 1.36× | 1.35× | 1.34× |
| update-center | 1.31× | 1.28× | 1.30× | 1.30× | 1.26× |
| small dict | 1.28× | 1.01× | 1.24× | 1.17× | 1.19× |
| empty dict | 1.15× | 1.05× | 1.17× | 1.03× | 1.19× |
| int | 1.15× | 1.13× | 1.25× | 1.08× | 1.29× |
| float | 1.26× | 1.23× | 1.30× | 1.08× | 1.24× |
| short string | 1.26× | 1.17× | 1.29× | 1.17× | 1.30× |
| mixed list | 1.10× | 1.00× | 1.20× | 1.11× | 1.18× |
| corpus geomean | 1.27× | 1.27× | 1.27× | 1.29× | 1.28× |

To reproduce, build for each interpreter (see the README), then:

```bash
.venv-bench/bin/python bench/bench_regression.py --cpu 2 --pairs 40 --batch-ms 20 --output build/bench/regression-3.12.json
.venv-py3.14/bin/python bench/bench_paired.py build/jsonexamples --cpu 2 --pairs 40 --batch-ms 20 --micro --output build/bench/paired-3.14.json
```

## Profile-driven encoder improvements (CPython 3.14)

[Reports](python-versions/): `shapes-3.14-before.*`, `shapes-3.14-after.*`,
`regression-3.14-perf.*`. Method: `bench/bench_shapes.py` times mojson against
orjson on synthetic payloads that each isolate one code path; callgrind
(instruction counts and line attribution on a line-table build) located the
cost; every candidate was built as a separate variant and measured against the
previous one with `bench_regression.py` (alternating pairs, one process) and,
where timing noise was in doubt, with deterministic instruction counts. The
"before" build is commit `3fc7e09` (the version port, no performance changes).

Findings and changes, in the order they were made:

- **3.14 small-int cache.** On 3.14+ the cached small ints carry an
  immortality bit in `lv_tag`; the 4-wide integer batch's tag test rejected
  them, so lists of small ints took the scalar writer at 0.72× of orjson.
  Masking the bit: 1.35×.
- **Literals and empty containers.** `true`/`false`/`{}`/`[]` list items and
  dict values went through the generic dispatch and a nested call; written
  inline they go from about 0.3× to 0.85–0.9×.
- **Dict entries straight from the key table.** `PyDict_Next` cost about 50
  instructions per entry; unicode-keyed combined tables are now walked
  directly (layout identical on 3.11–3.15, asserted by the C shim), with the
  table re-validated after any nested call. Keeping the walker to two words
  mattered: a five-word version spilled the dict loop's registers and lost
  10% on dict-heavy files, which the deterministic counts did not show.
- **Short leaf lists of floats** (coordinate pairs) are written in place:
  canada +10%.
- **Native records for sorted and non-str keys.** The C shim snapshots the
  entries, converts int/float/bool/None keys to text without temporary Python
  strings, and sorts with an inlined insertion/quicksort instead of `qsort`
  callbacks (glibc's callback mergesort, `memcmp` and refcounting were four
  times the cost of writing the dict). `OPT_SORT_KEYS` 0.48× → 1.04×,
  `OPT_NON_STR_KEYS` 0.34× → 1.52×.
- **`OPT_INDENT_2` on the compact writers** as a compile-time variant instead
  of the generic walker: 0.69× → 1.51×. The generic walker now serves only
  `OPT_STRICT_INTEGER` and `OPT_PASSTHROUGH_SUBCLASS`.

Not changed, with reasons: the float formatter (already the zmij core, about
300 instructions per 17-digit value; orjson now uses zmij as well), long and
escape-heavy strings (memory bound, at parity), and lists mixing `True`,
`False` and `None` at random (branch mispredictions in the type dispatch,
0.57×; left as is, since reordering the dispatch would cost the common types).

Corpus, 3.14, before the performance work → current (same process, 40 pairs):
geometric mean **1.179×**. twitter 1.37×, twitterescaped 1.35×, instruments
1.39×, random 1.32×, update-center 1.29×, github_events 1.29×, citm_catalog
1.22×, apache_builds 1.22×, canada 1.10×, marine_ik 1.08×; the numeric array
files (mesh, numbers) are unchanged within noise.

Shapes on 3.14, orjson time / mojson time (before → after):

| Shape | Before | After |
| --- | ---: | ---: |
| float pairs x10k (canada) | 1.06× | 1.09× |
| float pairs as tuples x10k | 1.09× | 1.11× |
| floats x10k flat | 1.28× | 1.25× |
| short floats x10k | 1.28× | 1.24× |
| small ints x10k | 0.72× | 1.35× |
| large ints x10k | 1.98× | 1.92× |
| mixed scalars x10k | 0.89× | 0.84× |
| ascii str 8 x2k | 1.17× | 1.08× |
| ascii str 32 x2k | 1.26× | 1.12× |
| ascii str 128 x2k | 1.21× | 1.10× |
| ascii str 1024 x2k | 0.93× | 0.95× |
| ascii str 128 escapes x2k | 0.96× | 0.96× |
| unicode str 32 x2k | 1.14× | 1.00× |
| dict 3 str keys x2k | 1.04× | 1.29× |
| dict 20 str keys x500 | 1.06× | 1.63× |
| dict 20 str->str x500 | 1.14× | 1.65× |
| dict 100 int values x100 | 0.87× | 1.22× |
| nested dict depth 6 x500 | 0.85× | 0.81× |
| list of empty dicts x10k | 0.29× | 0.88× |
| list of empty lists x10k | 0.27× | 0.84× |
| bools and none x10k | 0.34× | 0.57× |
| wide dict 10k keys | 0.86× | 1.17× |
| wide dict 10k str values | 0.90× | 1.12× |
| dict with float pair values x2k | 1.12× | 1.16× |
| str keys sorted x500 | 0.48× | 1.04× |
| nonstr keys unsorted x500 | 0.34× | 1.52× |
| nonstr keys sorted x500 | 0.34× | 1.52× |
| indent 2 dict 20 keys x500 | 0.69× | 1.51× |

## Reflex PR 6116

[Integration and reproduction details](../REFLEX.md).
The orjson baseline keeps the original PR socket codec; the mojson integration
replaces its stdlib retry with native socket serialization. Socket gains include
that framework change. Full event processing excludes network and rendering.

- [Paired benchmarks](socket-reflex-benchmark.json) / [text summary](socket-reflex-benchmark.txt)
- [Validation summary](socket-reflex-validation.json)
- [Fallback-disabled packet validation](socket-native-validation.json)
- [Profile summary](socket-profile-summary.json)
- [Default-path regression](socket-regression.json) / [text summary](socket-regression.txt)
- [Browser evidence](socket-reflex-browser.json)

These measurements describe this machine and these payloads. Sorted Reflex
artifacts remain about 2% slower; indentation is around parity.

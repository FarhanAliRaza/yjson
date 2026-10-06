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
geometric mean **1.178×** (current / original). The port itself changed nothing
(measured 1.002× before the performance work below, with identical 3.12 machine
code in the hot paths); the gain comes from the encoder improvements. The
encoder's absolute times are flat across interpreters: the hot paths differ only
by compile-time constants, and the ratios below move with orjson's own speed
on each interpreter.

| Case | 3.11 | 3.12 | 3.13 | 3.14 | 3.15 |
| --- | ---: | ---: | ---: | ---: | ---: |
| apache_builds | 1.26× | 1.38× | 1.27× | 1.25× | 1.33× |
| canada | 1.07× | 1.12× | 1.04× | 1.05× | 1.09× |
| citm_catalog | 1.21× | 1.26× | 1.24× | 1.28× | 1.26× |
| github_events | 1.44× | 1.32× | 1.38× | 1.34× | 1.35× |
| gsoc-2018 | 1.09× | 1.15× | 1.13× | 1.14× | 1.08× |
| instruments | 1.73× | 1.44× | 1.47× | 1.53× | 1.51× |
| marine_ik | 1.16× | 1.20× | 1.21× | 1.24× | 1.24× |
| mesh | 1.16× | 1.23× | 1.20× | 1.25× | 1.28× |
| mesh.pretty | 1.21× | 1.28× | 1.25× | 1.29× | 1.32× |
| numbers | 1.30× | 1.41× | 1.51× | 1.53× | 1.54× |
| random | 1.37× | 1.40× | 1.39× | 1.43× | 1.41× |
| twitter | 1.38× | 1.39× | 1.37× | 1.36× | 1.41× |
| twitterescaped | 1.36× | 1.32× | 1.38× | 1.38× | 1.42× |
| update-center | 1.31× | 1.36× | 1.34× | 1.33× | 1.25× |
| small dict | 1.20× | 1.18× | 1.22× | 1.11× | 1.22× |
| empty dict | 1.04× | 1.13× | 1.18× | 1.07× | 1.15× |
| int | 1.17× | 1.18× | 1.28× | 1.13× | 1.22× |
| float | 1.21× | 1.21× | 1.24× | 1.23× | 1.23× |
| short string | 1.17× | 1.28× | 1.28× | 1.17× | 1.27× |
| mixed list | 1.12× | 1.20× | 1.18× | 1.16× | 1.21× |
| corpus geomean | 1.28× | 1.30× | 1.29× | 1.31× | 1.31× |

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
- **Containers first in the type dispatch.** The list and dict loops write
  every scalar inline, so the generic dispatch is reached almost only for
  nested containers; testing dict and list before str/int/float/None/bool
  saves five compares per container. Corpus neutral (+0.6%, fewer
  instructions), lists of empty containers 0.83× → 0.95–0.97×, single-key
  nested dicts 0.78× → 0.89×, 3-key dicts 1.21× → 1.31×.

Not changed, with reasons: the float formatter (already the zmij core, about
300 instructions per 17-digit value; orjson now uses zmij as well), and long
and escape-heavy strings (memory bound, at parity).

Tried and rejected: a branch-free path for `null`/`true`/`false`. Lists mixing
`True`, `False` and `None` at random run at 0.57× because the test that
separates `None` from the bools is a data-dependent branch (homogeneous lists
of any of the three are at 0.7×, so the rest is the per-element loop cost).
Choosing the literal with masks and testing both singletons in one non-short-
circuit expression brought that shape to 1.0×, but the type pointer it needs
is either a loop-invariant register (which spills the list loop's hot state)
or a load per element; both variants cost 3% on the corpus in paired runs
with the roles swapped, and a third attempt that hoisted the pointer in the
list loop only cost 1.5% and gave back the empty-container gain, so the
compact loops keep the branch. Exponential
back-off after a failed SIMD batch (for lists mixing ints with other types)
was rejected the same way: one more live counter cost the integer array files
8%.

### Other libraries

[all-libraries-3.14.txt](python-versions/all-libraries-3.14.txt):
`bench/bench_all_libraries.py` on 3.14 (best of 21 rotated rounds, CPU 2;
less robust than the paired method, so mojson's own column differs slightly
from the paired tables above). Speed relative to orjson, corpus geomean:

| mojson | orjson | msgspec 0.22 | ujson 6.0 | python-rapidjson 1.25 | json (stdlib) | simplejson 4.2 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1.15× | 1.00× | 0.59× | 0.19× | 0.12× | 0.10× | 0.07× |

orjson is the fastest of the other serializers on this workload; msgspec's
encoder is next. Both read CPython objects directly and use the same class of
algorithms (zmij floats, SIMD escape scans, direct dict iteration), so the
remaining differences are in dispatch and per-element overhead rather than in
a different algorithm.

Corpus, 3.14, before the performance work → current (same process, 40 pairs):
geometric mean **1.184×**. twitter 1.37×, twitterescaped 1.35×, instruments
1.39×, random 1.32×, update-center 1.29×, github_events 1.29×, citm_catalog
1.22×, apache_builds 1.22×, canada 1.10×, marine_ik 1.08×; the numeric array
files (mesh, numbers) are unchanged within noise.

Shapes on 3.14, orjson time / mojson time (before → after):

| Shape | Before | After |
| --- | ---: | ---: |
| float pairs x10k (canada) | 0.95× | 1.14× |
| float pairs as tuples x10k | 1.03× | 1.12× |
| floats x10k flat | 1.24× | 1.28× |
| short floats x10k | 1.22× | 1.24× |
| small ints x10k | 0.73× | 1.36× |
| large ints x10k | 2.01× | 1.91× |
| mixed scalars x10k | 0.87× | 0.84× |
| ascii str 8 x2k | 1.15× | 1.11× |
| ascii str 32 x2k | 1.26× | 1.15× |
| ascii str 128 x2k | 1.21× | 1.15× |
| ascii str 1024 x2k | 1.00× | 0.97× |
| ascii str 128 escapes x2k | 0.95× | 0.95× |
| unicode str 32 x2k | 1.10× | 0.99× |
| dict 3 str keys x2k | 1.00× | 1.28× |
| dict 20 str keys x500 | 1.04× | 1.58× |
| dict 20 str->str x500 | 1.14× | 1.60× |
| dict 100 int values x100 | 0.88× | 1.22× |
| nested dict depth 6 x500 | 0.86× | 0.86× |
| list of empty dicts x10k | 0.30× | 0.97× |
| list of empty lists x10k | 0.26× | 0.99× |
| bools and none x10k | 0.33× | 0.57× |
| wide dict 10k keys | 0.87× | 1.18× |
| wide dict 10k str values | 0.88× | 1.10× |
| dict with float pair values x2k | 1.05× | 1.15× |
| str keys sorted x500 | 0.48× | 0.99× |
| nonstr keys unsorted x500 | 0.36× | 1.53× |
| nonstr keys sorted x500 | 0.36× | 1.51× |
| indent 2 dict 20 keys x500 | 0.67× | 1.58× |

### Algorithms from the literature, and whether they apply

Surveyed for the remaining cost centers (floats, integers, string escaping,
type dispatch). Sources are linked from the main README's credits where used.

- **Shortest float formatting.** The frontier is Schubfach (Giulietti 2020)
  and its descendants: Dragonbox (Jeon), Tejú Jaguá (Neri, about 27% faster
  than Dragonbox and 2× Ryū), yy (ibireme) and xjb (2025 preprint), which
  the Żmij library combines into one 64×128-bit multiplication per double
  with SIMD digit output. mojson already ports Żmij's core and digit output,
  and orjson 3.12 now uses Żmij as well, so both sit on the same algorithm;
  canada and numbers are at 1.05–1.5× of orjson by loop structure, not by
  the conversion. A 2026 experimental review (Champagne Gareau, *Software:
  Practice and Experience*) covers the same family; it was not reachable from
  this environment, so the ranking above is from the libraries' own
  published benchmarks.
- **Integer formatting.** Champagne Gareau & Lemire, *Converting an Integer
  to a Decimal String in Under Two Nanoseconds* (2026): all eight digits of
  a value below 10^8 from two AVX-512 IFMA multiply-add instructions
  (`vpmadd52lo/hi` with per-lane reciprocals of 10^k), 1.4–2× the best
  table-based writers. It needs AVX-512 IFMA (Ice Lake / Zen 4 and newer);
  the machine used here has AVX-512 F/BW/VL but not IFMA, and the
  comparison target is a wheel without it, so it was not adopted. mojson's
  4-wide batch (Żmij's 16-bit-lane BCD trick) is the AVX2 equivalent and
  already writes small ints at 1.35× and large ints at 1.9× of orjson.
- **String escaping.** Lemire, *Escaping strings faster with AVX-512*
  (2022): expand 32 bytes to 64 with interleaved zeros, blend the escape
  characters in, then `vpcompressb` the unused bytes out, at 8.5 GB/s against
  2 GB/s for a lookup-table SIMD scan. It requires AVX-512 VBMI2, absent
  here. The portable part of the idea, producing the escape mask directly in
  a k-register (`vpcmpub`) instead of `pmovmskb`, needs only AVX-512BW/VL,
  is what orjson's wheel uses when the CPU allows, and was tried here: the
  escape scan gained a 64-byte k-mask variant and the build an `MCPU` knob.
  Built for x86-64-v4 on this Xeon (AVX-512 F/BW/VL, Cascade Lake class) the
  whole module came out 6% slower (corpus geomean 0.941×, every file down):
  the compiler widens all SIMD code to 512-bit registers (about 3,900 zmm
  instructions), which on this CPU generation lowers the clock for heavy
  512-bit use, and the string shapes themselves were mixed (+5% on 128-byte
  strings, −6% with escapes, −20% on non-ASCII). The default build stays
  AVX2; `MCPU=x86-64-v4 ./build.sh` is an opt-in for CPUs without that
  penalty (Sapphire Rapids, Zen 4 and newer), untested here.
- **Type dispatch.** Ertl & Gregg (2003) on indirect-branch misprediction
  in interpreters, and Rohou, Swamy & Seznec, *Branch prediction and the
  performance of interpreters: don't trust folklore* (2015): modern TAGE-
  class predictors handle dispatch on repeating patterns well, and nothing
  predicts a uniformly random type sequence. That matches the measurements:
  homogeneous lists dispatch at full speed, random mixes pay one mispredict
  per element in every library, and the branch-free literal path tried above
  removed it only by adding a per-element cost elsewhere.
- **Not applicable.** Zero-copy and schema-driven serialization work
  (Cornflakes, Cap'n Proto-style layouts, FlatBuffers) avoids text entirely;
  simdjson's parsing results (Langdale & Lemire 2019) concern the decoder,
  which mojson delegates to the standard library by design.

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

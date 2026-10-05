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

## CPython 3.12 - 3.15 (version port)

[Reports](python-versions/): `regression-3.12.*` and `paired-3.1x.*`.
Measured in a shared cloud container (Intel(R) Xeon(R) Processor @ 2.80GHz, AVX2 build, CPU 2 pinned,
40 alternating pairs, batches of at least 20 ms), Mojo 1.1.0, orjson 3.12.0 on
every interpreter: CPython 3.12.3, 3.13.14, 3.14.8 and 3.15.0rc3. The container
is slower and noisier than the desktop runs above, so compare columns here with
each other rather than with the table above.

`regression-3.12` loads the build from before the port and the ported build in
one process on CPython 3.12. Corpus geometric mean **1.010×** (ported / previous);
every case lies between 0.99× and 1.05× with overlapping middle halves, i.e. no
measurable change. The ported encoder's absolute times are also flat across
interpreters (for example `numbers` takes 209-214 µs and `mesh` 987-997 µs on all
four), as expected since the hot paths only differ by compile-time constants.
The ratios below move with orjson's own speed on each interpreter.

| Case | 3.12 | 3.13 | 3.14 | 3.15 |
| --- | ---: | ---: | ---: | ---: |
| apache_builds | 1.14× | 1.03× | 1.10× | 1.09× |
| canada | 1.08× | 0.92× | 0.95× | 0.99× |
| citm_catalog | 1.05× | 1.03× | 1.04× | 1.04× |
| github_events | 1.03× | 1.04× | 1.05× | 1.03× |
| gsoc-2018 | 1.01× | 0.98× | 0.98× | 0.92× |
| instruments | 1.07× | 1.06× | 1.09× | 1.08× |
| marine_ik | 1.19× | 1.18× | 1.13× | 1.14× |
| mesh | 1.26× | 1.30× | 1.28× | 1.28× |
| mesh.pretty | 1.31× | 1.35× | 1.32× | 1.32× |
| numbers | 1.40× | 1.51× | 1.52× | 1.50× |
| random | 1.08× | 1.08× | 1.09× | 1.09× |
| twitter | 1.01× | 1.01× | 1.01× | 0.99× |
| twitterescaped | 0.99× | 1.01× | 1.01× | 1.02× |
| update-center | 0.99× | 1.03× | 1.01× | 1.01× |
| small dict | 1.03× | 0.93× | 1.05× | 1.11× |
| empty dict | 1.14× | 1.12× | 1.07× | 1.16× |
| int | 1.23× | 1.18× | 1.15× | 1.20× |
| float | 1.25× | 1.18× | 1.17× | 1.17× |
| short string | 1.29× | 1.19× | 1.21× | 1.25× |
| mixed list | 1.17× | 1.08× | 1.14× | 1.20× |
| corpus geomean | 1.11× | 1.10× | 1.10× | 1.10× |

To reproduce, build for each interpreter (see the README), then:

```bash
.venv-bench/bin/python bench/bench_regression.py --cpu 2 --pairs 40 --batch-ms 20 --output build/bench/regression-3.12.json
.venv-py3.14/bin/python bench/bench_paired.py build/jsonexamples --cpu 2 --pairs 40 --batch-ms 20 --micro --output build/bench/paired-3.14.json
```

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

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

# Default Mojo implementation: local benchmarks

Measured on AMD Ryzen 5 5600 (AVX2), 2026-10-08 in Asia/Karachi. This report is a snapshot of the production migration; exploratory encoder/decoder variants are excluded. [Metadata and binary/source hashes](metadata.json), [summary](summary.json).

Completed all nine benchmark stages on the current production Mojo build. CPython 3.13.14, orjson 3.12.0; core comparisons use 40 alternating pairs, batches ≥10 ms, pinned to CPU 5. Lower times are better. Speedup is orjson time ÷ Mojo time; values below 1 mean orjson leads.

Corpus geometric means: encoding **1.525×**; original decoding **1.192×**; compact decoding **1.190×** versus orjson. Timing samples and spread are saved in the linked results. The investigation’s diagnostic optimizations are excluded.

Encoding corpus and microbenchmarks — µs/call:

| Case | Mojo | orjson | Speedup |
|---|---:|---:|---:|
| apache_builds | 26.357 | 42.551 | 1.614× |
| canada | 1790.010 | 2535.376 | 1.417× |
| citm_catalog | 368.307 | 387.318 | 1.051× |
| github_events | 10.326 | 17.654 | 1.709× |
| gsoc-2018 | 346.663 | 502.731 | 1.452× |
| instruments | 44.704 | 75.659 | 1.693× |
| marine_ik | 2662.595 | 3382.796 | 1.270× |
| mesh | 618.676 | 842.668 | 1.363× |
| mesh.pretty | 614.160 | 853.443 | 1.389× |
| numbers | 127.653 | 194.568 | 1.524× |
| random | 159.479 | 266.414 | 1.671× |
| twitter | 113.483 | 200.950 | 1.775× |
| twitterescaped | 112.948 | 200.866 | 1.779× |
| update-center | 146.173 | 276.959 | 1.875× |
| small dict | 0.087 | 0.099 | 1.143× |
| empty dict | 0.065 | 0.070 | 1.073× |
| int | 0.065 | 0.071 | 1.102× |
| float | 0.076 | 0.087 | 1.142× |
| short string | 0.061 | 0.074 | 1.207× |
| mixed list | 0.093 | 0.109 | 1.163× |

Decoding original documents — µs/call. C is the former implementation; ctypes calls the same Mojo parser through Python’s ctypes wrapper.

| Case | Mojo | orjson | C baseline | Mojo ctypes | Speedup |
|---|---:|---:|---:|---:|---:|
| apache_builds | 146.59 | 184.92 | 150.37 | 146.28 | 1.261× |
| canada | 4150.82 | 4333.76 | 4225.74 | 4061.88 | 1.044× |
| citm_catalog | 1630.62 | 1849.07 | 1699.26 | 1631.91 | 1.134× |
| github_events | 57.26 | 71.12 | 61.70 | 59.85 | 1.242× |
| gsoc-2018 | 1991.87 | 4173.81 | 2076.07 | 1979.73 | 2.095× |
| instruments | 245.63 | 273.61 | 255.67 | 248.99 | 1.114× |
| marine_ik | 5803.75 | 10358.27 | 5845.62 | 5801.99 | 1.785× |
| mesh | 1297.12 | 1467.19 | 1443.15 | 1301.26 | 1.131× |
| mesh.pretty | 1720.37 | 1811.68 | 1782.07 | 1725.95 | 1.053× |
| numbers | 180.17 | 204.18 | 188.40 | 181.58 | 1.133× |
| random | 1288.22 | 1258.71 | 1358.14 | 1279.63 | 0.977× |
| twitter | 812.61 | 801.43 | 844.98 | 808.68 | 0.986× |
| twitterescaped | 890.09 | 853.58 | 909.28 | 883.64 | 0.959× |
| update-center | 899.77 | 1092.79 | 951.54 | 888.13 | 1.215× |
| 1000 records | 487.45 | 559.71 | 503.44 | 484.96 | 1.148× |

Decoding compact documents — µs/call:

| Case | Mojo | orjson | C baseline | Mojo ctypes | Speedup |
|---|---:|---:|---:|---:|---:|
| apache_builds | 131.47 | 178.91 | 136.94 | 132.05 | 1.361× |
| canada | 4284.54 | 4446.35 | 4348.27 | 4177.29 | 1.038× |
| citm_catalog | 1462.25 | 1645.99 | 1511.85 | 1463.93 | 1.126× |
| github_events | 55.86 | 72.77 | 58.75 | 54.92 | 1.303× |
| gsoc-2018 | 2066.01 | 5587.51 | 2245.61 | 2199.33 | 2.704× |
| instruments | 220.08 | 253.40 | 245.16 | 223.63 | 1.151× |
| marine_ik | 5627.77 | 6237.31 | 5741.37 | 5657.08 | 1.108× |
| mesh | 1225.52 | 1382.96 | 1386.91 | 1227.49 | 1.128× |
| mesh.pretty | 1446.04 | 1496.92 | 1525.26 | 1445.30 | 1.035× |
| numbers | 180.72 | 204.78 | 187.52 | 181.44 | 1.133× |
| random | 1244.40 | 1228.37 | 1332.82 | 1245.61 | 0.987× |
| twitter | 733.13 | 757.23 | 774.83 | 732.73 | 1.033× |
| twitterescaped | 874.44 | 840.17 | 898.89 | 871.76 | 0.961× |
| update-center | 880.15 | 1079.20 | 948.44 | 879.93 | 1.226× |

Decoding 2,000-string payloads — µs/call:

| Case | Mojo | orjson | C baseline | Mojo ctypes | Speedup |
|---|---:|---:|---:|---:|---:|
| decode ascii | 50.39 | 96.09 | 49.47 | 52.23 | 1.907× |
| decode utf8 | 253.86 | 184.19 | 253.92 | 257.14 | 0.726× |
| decode unicode escapes | 751.94 | 520.56 | 664.58 | 755.15 | 0.692× |
| decode ascii escapes | 449.21 | 256.28 | 451.88 | 454.83 | 0.571× |

Typed decoding, 1,000 records — µs/call:

| Result type | Mojo | C baseline | C/Mojo |
|---|---:|---:|---:|
| Record | 419.18 | 451.71 | 1.078× |
| SlotRecord | 364.70 | 389.39 | 1.068× |

Typed/untyped library comparison, 1,000 records — µs/call. Different result types are shown explicitly.

| Decoder/result | Time | Time / Mojo Record |
|---|---:|---:|
| yjson.loads -> dicts | 462.9 | 1.01 |
| orjson.loads -> dicts | 537.2 | 1.17 |
| orjson + dataclasses by hand | 1471.2 | 3.20 |
| yjson type=list[Record] | 459.2 | 1.00 |
| yjson type=list[SlotRecord] | 369.2 | 0.80 |
| msgspec -> dicts | 545.3 | 1.19 |
| msgspec -> list[Record] dataclass | 601.3 | 1.31 |
| msgspec -> list[Struct] | 391.2 | 0.85 |

Encoding features, 100 items — µs/call:

| Feature | Mojo | orjson | Speedup |
|---|---:|---:|---:|
| compact | 5.599 | 6.315 | 1.135× |
| indent | 6.797 | 8.404 | 1.237× |
| sorted | 7.450 | 7.600 | 1.019× |
| indent + sorted | 8.397 | 9.871 | 1.174× |
| strict integers | 7.447 | 6.289 | 0.847× |
| non-str keys | 5.529 | 5.003 | 0.910× |
| datetime | 1.493 | 1.997 | 1.320× |
| UUID | 2.063 | 3.175 | 1.530× |
| dataclass | 5.063 | 5.082 | 1.006× |
| Enum | 2.832 | 7.000 | 2.471× |
| default callback | 4.072 | 4.436 | 1.090× |

Encoding Python types — ns/item, 100-item batches:

| Type | Mojo | orjson | Speedup |
|---|---:|---:|---:|
| datetime naive | 14.6 | 18.6 | 1.28× |
| datetime utc | 15.3 | 72.3 | 4.73× |
| datetime zoneinfo | 50.7 | 86.1 | 1.70× |
| date | 11.9 | 13.1 | 1.10× |
| time | 13.2 | 19.6 | 1.49× |
| UUID | 18.9 | 32.0 | 1.69× |
| dataclass | 50.3 | 51.6 | 1.03× |
| dataclass slots | 179.1 | 283.1 | 1.58× |
| dataclass private | 44.0 | 44.1 | 1.00× |
| Enum | 28.4 | 68.6 | 2.41× |
| IntEnum | 11.9 | 8.1 | 0.68× |
| dict of datetimes | 47.9 | 67.6 | 1.41× |
| default callback | 40.1 | 40.8 | 1.02× |

Encoding NumPy arrays — ms/call, minimum of seven runs:

| Array | Mojo | orjson | stdlib json |
|---|---:|---:|---:|
| f64 1M | 12.09 | 19.36 | 339.1 |
| f64 1M 5% NaN | 13.18 | 18.59 | 328.4 |
| f64 1000x1000 | 11.24 | 20.06 | 325.8 |
| f32 1M | 14.73 | 20.02 | 328.1 |
| i64 1M +-1e6 | 2.63 | 5.31 | 60.6 |
| i64 1M +-1e15 | 3.54 | 8.70 | 68.8 |
| bool 1M | 0.98 | 2.17 | 15.4 |

Encoding synthetic shapes — µs/call:

| Shape | Mojo | orjson | Speedup |
|---|---:|---:|---:|
| float pairs x10k (canada) | 320.94 | 456.31 | 1.423× |
| float pairs as tuples x10k | 313.75 | 465.48 | 1.482× |
| floats x10k flat | 262.11 | 382.68 | 1.459× |
| short floats x10k | 130.07 | 192.34 | 1.472× |
| small ints x10k | 25.84 | 35.46 | 1.368× |
| large ints x10k | 138.31 | 203.53 | 1.464× |
| mixed scalars x10k | 96.92 | 85.31 | 0.883× |
| ascii str 8 x2k | 8.34 | 11.39 | 1.358× |
| ascii str 32 x2k | 9.28 | 11.71 | 1.264× |
| ascii str 128 x2k | 13.09 | 20.16 | 1.541× |
| ascii str 1024 x2k | 56.67 | 107.24 | 1.891× |
| ascii str 128 escapes x2k | 644.82 | 511.70 | 0.794× |
| unicode str 32 x2k | 9.57 | 11.56 | 1.208× |
| dict 3 str keys x2k | 54.84 | 60.05 | 1.095× |
| dict 20 str keys x500 | 55.75 | 86.88 | 1.558× |
| dict 20 str->str x500 | 58.56 | 122.17 | 2.087× |
| dict 100 int values x100 | 65.94 | 86.92 | 1.319× |
| nested dict depth 6 x500 | 50.00 | 43.11 | 0.862× |
| list of empty dicts x10k | 33.18 | 27.36 | 0.825× |
| list of empty lists x10k | 31.99 | 26.39 | 0.826× |
| bools and none x10k | 39.52 | 23.73 | 0.601× |
| wide dict 10k keys | 69.99 | 87.12 | 1.248× |
| wide dict 10k str values | 73.78 | 127.25 | 1.724× |
| dict with float pair values x2k | 137.56 | 157.48 | 1.145× |
| str keys sorted x500 | 135.45 | 168.14 | 1.244× |
| nonstr keys unsorted x500 | 221.59 | 344.87 | 1.567× |
| nonstr keys sorted x500 | 217.72 | 347.19 | 1.594× |
| indent 2 dict 20 keys x500 | 69.72 | 116.82 | 1.675× |

Encoder outputs and decoded values matched the references in all benchmark checks. Binary hashes were unchanged across the run. These are local timing results; background activity can affect small differences. Encoder speedups use paired median ratios; decoder speedups shown here divide the separate median times. Features/types use CPU 2; other measurements use CPU 5. Types use 20 pairs; the other paired stages use 40. All raw tables, timings, interquartile spreads and per-pair samples are saved alongside this report.

## Reproduction

Build the production extension and the C baseline using Mojo 1.1.0 and the benchmark interpreter, then fetch the corpus:

```bash
MOJO="$PWD/.venv-mojo/bin/mojo" ./build.sh
./bench/c_decoder/build.sh
.venv-bench/bin/python tools/fetch_corpus.py
PYTHONHASHSEED=0 .venv-bench/bin/python bench/bench_paired.py build/jsonexamples --cpu 5 --pairs 40 --micro --output build/encoding.json
PYTHONHASHSEED=0 .venv-bench/bin/python bench/mojo_decoder/compare.py build/jsonexamples --cpu 5 --pairs 40 --compact --ctypes --output build/decoding.json
PYTHONHASHSEED=0 .venv-bench/bin/python bench/bench_shapes.py --cpu 5 --pairs 40 --output build/shapes.json
PYTHONHASHSEED=0 .venv-bench/bin/python bench/bench_features.py
PYTHONHASHSEED=0 .venv-bench/bin/python bench/bench_types.py
PYTHONHASHSEED=0 taskset -c 5 .venv-bench/bin/python bench/bench_numpy.py
PYTHONHASHSEED=0 taskset -c 5 .venv-bench/bin/python bench/bench_typed.py --pairs 40
```

For the extra string-decoding measurements, each document is an array containing 2,000 copies of the token below. These spellings specify the input bytes, not an encoding performed inside the timed region:

```python
tokens = {
    "ASCII": b'"' + b'x' * 128 + b'"',
    "UTF-8": ('"' + 'é' * 64 + '"').encode(),
    "Unicode escapes": b'"' + br'\u00e9' * 64 + b'"',
    "ASCII escapes": b'"' + br'a\nb\tc' * 21 + b'"',
}
documents = {name: b'[' + b','.join([token] * 2000) + b']' for name, token in tokens.items()}
```

The separate typed-C comparison uses the same 1,000-record data and annotations as `bench/bench_typed.py`, measured by `bench/mojo_decoder/compare.py`'s `measure` function with C/Mojo functions, 40 pairs, and a 10 ms batch target. The string measurements use that function with C, Mojo, orjson, and ctypes. Run those stages sequentially on CPU 5; do not overlap benchmarking processes.

Paired samples are preserved in the JSON reports for encoding, corpus/string decoding, features, and typed C/Mojo comparisons. The shape report preserves medians and quartiles; Python type and NumPy results preserve the original script output. Encoder speedups use paired median ratios, whereas decoding's orjson/Mojo column divides the two separate median times. Typed measurements in separate stages can differ through allocator state and system activity; compare backends within the same stage.

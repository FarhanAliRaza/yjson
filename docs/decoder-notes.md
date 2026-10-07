# Decoder notes: what was slow, what worked, what did not

The optimized Mojo parser is now the default in `src/decoder.mojo`, including
`loads(type=...)`. The former C parser is preserved in `bench/c_decoder/` as a
separately built baseline. Measurements below document the optimization work
before promotion; current comparisons use shipping `yjson.loads`.

Findings from the work that replaced `loads`, added `loads(type=...)`, ported the decoder
to Mojo and fixed the encoder's large-output allocation. Numbers are from CPython 3.13 on
a shared Xeon with orjson 3.12.0 and msgspec 0.22.0 unless noted otherwise;
run-to-run noise there was about 5%. The later Mojo measurements use a Ryzen 5 5600.

## Why the old `loads` was slow

It called `json.loads` with a Python hook for every integer and float, then walked the
whole result again in Python to check depth and surrogates. That is a Python call per
number and a second visit per value: 3.5–5× slower than plain `json.loads` and about 10×
slower than orjson. Nothing short of parsing in C fixes that.

## Where decoding time goes

Instruction counts under callgrind for the final C decoder:

| Workload | Parser itself | CPython creating objects | libc |
| --- | ---: | ---: | ---: |
| 1,000 records | 41% | 52% | 6% |
| twitter.json | 41% | 52% | 6% |
| mesh.json (numeric arrays) | 69% | 26% | 4% |
| records into slotted dataclasses | 69% | 23% | 8% |

On object documents the largest single item is dict insertion (`PyDict_SetItem` plus its
internals, about a third of all instructions), then the object loop, then `str` creation.
No parser design changes the CPython half; only a schema does, which is what `type=` is for.

## What made the C decoder fast, with measured effect

- Strict recursive descent that builds Python objects directly; arrays from a value stack
  so each list is allocated once at its final size.
- SSE2 scans for string ends and, out of line, for runs of indentation. A first version
  scanned whitespace with SIMD unconditionally and lost 6% on compact numeric files: a
  single space after a comma paid the vector setup. One or two byte checks first fixed it.
- A key cache (2,048 slots, keys up to 64 bytes) so repeated keys share one `str` and its
  hash, and next-key prediction: the key that followed the previous key last time is tried
  by a direct compare before hashing. Records −6%, citm_catalog −18%.
- Numbers: one unrolled step per digit relying on the input's terminating NUL, eight
  digits at a time where a word of digits is present, Clinger's exact path, then
  Eisel-Lemire with a generated 128-bit power-of-five table, CPython's `strtod` only past
  19 significant digits. canada.json went from 0.27× to 1.0× orjson.
- Dispatch on the value's first byte in place inside the container loops instead of
  calling a generic value parser that re-skipped whitespace: about 3 ns per element.
- A loop of their own for numeric arrays, with the number parser taking the cursor as an
  argument so it never goes through the parser struct: numbers.json −7%, canada −9%.

Tried and dropped: presizing dicts at the closing brace (slower on every document, 2–5%);
scan-then-convert numbers (more instructions than the unrolled chain); the eight-digit step
on integer parts (cost more than it saved on short integers, canada +4%). Memoryviews are
copied to `bytes` so every input buffer is NUL-terminated and immutable while the typed
path runs user code.

## Typed decoding and msgspec

`loads(type=...)` compiles each annotation once into a plan; dataclass keys are matched by
a direct compare against the field expected next, then a hash table, then a dict for keys
needing unescaping. Instances are allocated with `tp_alloc`; slotted fields are stored in
place, others through `PyObject_GenericSetAttr`.

msgspec's Struct was 20% faster at first. The whole gap was key hashing: msgspec compares
each key with the field it expects next and hashes only on a miss. Doing the same brought
slotted dataclasses level with Structs. msgspec's other trick, deferring GC tracking until a
container is stored, measured no difference and is unsafe for dataclasses, whose slots can
be assigned containers later without re-tracking.

## The Mojo port

The original experiment (`bench/mojo_decoder/`) measured 6–9% slower on the shared
Xeon. Its parser preceded C's numeric-array and fractional eight-digit optimizations
(commit `27530ac`), and its Python entry point used ctypes. Those differences must be
controlled before attributing the gap to the compiler.

On the Ryzen, the original parser behind a native Python entry point was only 0.5%
slower than current C across the 14-file corpus, and 1.2% faster than C from before
the numeric optimizations. On numbers.json, current C took 185.5 µs, old C 197.7 µs,
and original native Mojo 196.5 µs: the missing numeric optimizations explain that gap.
The port now keeps the numeric-array cursor local, consumes eight fractional digits
at a time in arrays, and uses a native entry point by default. A compile-time
specialization preserves the original scalar/object number path to avoid charging
short object-field numbers for the bulk-digit check. The paired Ryzen run before
the whitespace change put Mojo approximately level with C (1.000× C time) and at
1.158× orjson's speed
across the corpus. Numeric time fell 7.7% on numbers.json and 4.0% on mesh.json
against the original native port. Current results and reproduction commands are in
[the decoder benchmark README](../bench/mojo_decoder/README.md).

Historical instruction counts were similar (37.0M vs 37.8M in the number parser on
numbers.json). Changes to escape decoding, the whitespace bit mask, and signed
conversion to double improved particular workloads. The old observation that
clang-18 C was 3–7% slower than GCC C supports a possible code-generation effect;
it does not establish the cause of every remaining cycle. A later whitespace rewrite
produced simpler-looking bit-test assembly but lost about 1.2% overall and was dropped.

### Whitespace scanning

A later controlled test removed only whitespace outside strings, preserving numeric
spelling and quoted content. The Mojo/C time ratio fell from 1.050 to 1.001 on
instruments.json, from 1.033 to 0.920 on apache_builds.json, and from 1.047 to 1.016
on mesh.pretty.json. This identifies formatting as a substantial contributor, though
removing bytes also changes input alignment and does not isolate every cycle.

Callgrind on 25 warm decodes of instruments.json attributed 17,736,100 instructions
to Mojo's whitespace-run scanner versus 14,329,900 for C, 23.8% more. The integer
parser also used 29.7% more instructions, while `PyDict_SetItem` had identical
counts. Total instruction counts were lower for Mojo on some other documents even
when wall time was higher, so counts alone do not establish the timing cause.

The retained change splits the scalar whitespace checks into an ordinary-space
branch, an early exit for bytes above 13, and checks for LF/TAB/CR. This avoids the
shift, flag-combination, and conditional-move chain in the scalar prefix and tail.
The 16-byte SIMD loop and the initial compact-input whitespace mask stay intact.
A shortcut consuming eight ordinary spaces at once regressed the screening run
and was dropped. `compare.py --compact` now checks both input forms in the same
paired benchmark; current measurements are in the decoder benchmark README.

The final 40-pair run reduced time by 9.2% on apache_builds.json, 5.6% on
instruments.json, and 2.1% on mesh.pretty.json against the pre-whitespace native
parser. Across the 14 original documents, time fell 3.0%, with Mojo/C time at
0.963×; compact-input time was unchanged overall. Individual compact results
still varied, including a 1.7% regression on random.json.

Repeating the instruments profile with the same 25 warm/25 collected calls and
`PYTHONHASHSEED=0` reduced scanner instructions from 17,736,100 to 10,753,225
(39.4%) and total instructions from 112,401,294 to 105,694,339 (6.0%). Number-parser
and dictionary-insertion self counts were unchanged. This directly verifies lower
scanner instruction cost; it does not attribute every timing difference to it.
The saved report is `build/mojo-lag-profile/mojo-whitespace-after-instruments.callgrind.txt`,
and every timing sample is in `build/local-mojo-whitespace-benchmark.json`.

### Integer, float and cached-key paths

The remaining compact-instruments profile showed 8,943,625 instructions in Mojo's
number parser versus 6,893,550 in C across 25 collected calls. Disassembly showed
Eisel-Lemire checks hoisted onto integer paths and six saved registers in the
scalar number parser. The retained change returns integers immediately after the
digit chain, keeps general float conversion out of line, and leaves Clinger's
common exact conversion inline. The scalar parser now saves one register; its
instruments instruction count fell to 5,242,375, 41.4% below the prior Mojo version.
Total compact-instruments instructions fell 6.1% with all retained changes.

For general floats, LLVM had converted the subnormal branch into calculations and
conditional moves performed for normal values too. Moving subnormal rounding into
a separate non-inlined helper restored a branch around that work. The rounding
algorithm remains the same; regression tests compare exact float bits at normal,
subnormal, underflow, and overflow boundaries against orjson.

Lists with one to four elements now move their references with bounded scalar or
vector stores. Lengths two to four use overlapping first/last 16-byte copies; larger
lists keep `memcpy`. Cached ASCII keys of 8–16 bytes similarly compare first/last
eight-byte words without a libc call, with other lengths keeping `memcmp`.
On compact citm_catalog.json, this key change reduced `memcmp` self instructions
from 12,851,238 to 6,769,492 and total instructions by 1.1% against the preceding
numeric/list candidate. Tests cover key-length boundaries and mismatches at the
start, middle, and end, plus list ownership at varying parser-stack offsets.
On marine_ik.json, all retained changes reduced recorded total instructions from
2,565,973,371 to 2,453,449,079 (4.4%). Counts verify changes in executed work;
paired wall-time measurements determine whether they improved actual latency.

An additional out-of-line rounding-tie helper helped Canada but slowed the screening
average and was dropped. Scalar copies with one conditional store per short-list
item also failed to improve the screening average; the retained copy uses two
bounded vector stores instead. These findings concern this parser and compiler
build, and do not establish a universal Mojo penalty. Profiles, disassemblies, and
candidate samples are saved under `build/mojo-residual-profile/`.

Two final fresh-process runs, each with 40 paired batches of at least 10 ms on
CPU 5 and `PYTHONHASHSEED=0`, measured all 14 original files, 14 compact variants,
and the records payload. Neither run had an input with a paired median slower
than C. Mojo used 5.2–5.7% less time across the original corpus and 6.0–6.1% less
across compact variants. Some batch ranges still cross parity; these are measured
results for this machine and compiler, not a guarantee for every input or system.
Both reports, including every sample, are in
`build/local-mojo-residual-benchmark.json` and
`build/local-mojo-residual-benchmark-repeat.json`. The final implementation passed
502,254 differential checks with zero differences, 354 upstream parsing/JSONChecker
tests, and ten regression tests.

Mojo facts learned on the way:

- Converting `Pointer(to=local)` to an integer and back breaks aliasing: the optimizer
  dropped the local's initializer stores and the parser read uninitialized memory on every
  input. Functions taking the parser must be generic over the pointer's origin.
- `p[].ctx[]` copies the struct on each access; bind it with `ref`.
- `fn` is gone in Mojo 1.1 and `def` is non-raising by default, so there is no error-return
  ABI on calls.
- The stdlib's `unsafe_memcpy`/`unsafe_memcmp` were slower than the libc calls on short
  strings. Early experiments with 32-byte scans, hints alone, and host-CPU targeting
  showed no measurable gain.

## The Mojo compiler

Source inspected at the public `mojo/v1.1.0` tag, commit
`c6fa49f712fe15c99d0461ad2baaf2a15cbc58ce`. The installed compiler reports build
`8189361e`, so this is release-tag evidence rather than a verified identical source
revision for the binary.

- A [hand-built LLVM O3 pass pipeline](https://github.com/modular/modular/blob/c6fa49f712fe15c99d0461ad2baaf2a15cbc58ce/Mojo/lib/Compiler/ObjectCompiler/LLVMPassesPipeline.cpp),
  rather than proof of an identical pipeline to clang. The
  [object compiler](https://github.com/modular/modular/blob/c6fa49f712fe15c99d0461ad2baaf2a15cbc58ce/Mojo/lib/Compiler/ObjectCompiler/ObjectCompiler.cpp)
  supplies `PGOOpt = std::nullopt` in this path.
- The [automatic inliner](https://github.com/modular/modular/blob/c6fa49f712fe15c99d0461ad2baaf2a15cbc58ce/Mojo/lib/Transforms/AutomaticInline.cpp)
  uses an MLIR operation-count threshold, with TODOs for a richer cost model. This can
  explain why source changes affect inlining; it does not measure a decoding penalty.
- The target machine takes default `TargetOptions`, the CPU from `--mcpu` and features from
  `--target-features`; no fast-math. `--fp-mode contract=fast` is the one Mojo-specific
  default and fuses `a + b*c`.
- `--bitcode-libs` links external LLVM bitcode into the module before optimization (needed
  functions only, internalized), so C compiled with
  `clang --target=x86_64-unknown-linux-gnu -emit-llvm` inlines into Mojo. The triple must
  match exactly or the library is skipped silently. Possible bug: with the default parallel
  build the linked functions vanish without an error; `-j 1` works. Tried on the encoder:
  the helpers inlined, speed unchanged, because the encoder's hot paths never call them.

## The encoder's page faults

`dumps` of a 3 MB document took 2.2 ms in a fresh process and 0.6 ms inside the long-running
benchmark. Cause: the output buffer grows by doubling to 4 MB, and shrinking it in place with
`_PyBytes_Resize` left glibc with a freed 3 MB chunk; glibc sets its dynamic mmap threshold to
the size of the last freed mapped chunk, which stayed below the next call's 4 MB peak, so
every call mapped fresh pages: 751 minor faults per call. orjson avoids it by setting
`ob_size` and letting its `_PyBytes_Resize` return early, so the peak-size chunk is freed
whole and the threshold rises past the peak. yjson now does the same for results above 1 MB:
gsoc-2018 2477 µs → 651 µs, level with orjson, at the cost of a large result holding up to
twice its length until freed.

## Measuring

- Alternate builds in fresh processes when they share one extension and library path;
  drift otherwise hides 5% effects. In-process A/B is possible with separately loaded
  native modules and distinct shared-library SONAMEs. The Mojo benchmark rotates order
  within paired batches, pins a selected CPU, and saves every sample with `--output`.
- Use instruction counts (callgrind) as the noise-free metric to decide what a change did;
  wall time to decide whether it matters. A profile of self cost per function finds the
  outlier (an un-inlined `push`, an escape chain) in one run.
- Fresh-process timings differ from long-process ones through the allocator, not the code;
  pin glibc with `mallopt` when that is not the thing under test.
- Timing `dumps` on objects from different loaders differs by up to 1.6 ms on canada.json
  for both libraries; keep the loader fixed across comparisons.
- Keeping benchmark results alive in a list while timing inflates later runs through
  memory pressure.

## Still open

- Non-ASCII text goes through `PyUnicode_DecodeUTF8`, 11% of twitter.json; a custom decoder
  into the right `str` kind could win part of that.
- Integers in arrays still cost 2–3 ns per element more than orjson's, with no counter that
  explains it.
- `loads(type=...)` has no `rename`, `Enum`, `datetime` or union support beyond `Optional`.

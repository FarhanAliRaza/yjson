# Decoder notes: what was slow, what worked, what did not

Findings from the work that replaced `loads`, added `loads(type=...)`, ported the decoder
to Mojo and fixed the encoder's large-output allocation. Numbers are from CPython 3.13 on
a shared Xeon with orjson 3.12.0 and msgspec 0.22.0; run-to-run noise there was about 5%.

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

A port on the same algorithm (`bench/mojo_decoder/`) decodes identically and runs 6–9%
slower than the C decoder. Instruction counts are the same (37.0M vs 37.8M in the number
parser on numbers.json). Three idioms the LLVM backend lowered worse than GCC accounted for
the rest and were fixed by writing them as the C does: escape decoding through a table and
a `switch` rather than compare chains (twitterescaped 1.21× → 1.03×), the whitespace test
as one bit test rather than an 18-instruction branchless sequence, and a signed rather than
unsigned conversion to double where the range check allowed it. The remainder is the
backend: the same `decoder.c` built with clang-18 is 3–7% slower than with GCC on the same
files, and the Mojo port is within about 3% of that clang build.

Mojo facts learned on the way:

- Converting `Pointer(to=local)` to an integer and back breaks aliasing: the optimizer
  dropped the local's initializer stores and the parser read uninitialized memory on every
  input. Functions taking the parser must be generic over the pointer's origin.
- `p[].ctx[]` copies the struct on each access; bind it with `ref`.
- `fn` is gone in Mojo 1.1 and `def` is non-raising by default, so there is no error-return
  ABI on calls.
- The stdlib's `unsafe_memcpy`/`unsafe_memcmp` were slower than the libc calls on short
  strings; 32-byte scans, `likely`/`unlikely` hints and host-CPU targeting changed nothing.

## The Mojo compiler

Read from the Modular repository (`Mojo/lib/Compiler`, `Mojo/include/Mojo/KGENPasses.td`):

- A hand-built but stock LLVM O3 pipeline. `--optimization-level` is 0 or 3. No PGO, no
  LTO, no user-facing pass or backend options; the automatic inliner's only options are
  debug-info handling and the optimization level.
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

- Alternate builds in fresh processes (`subprocess` per sample) when comparing two builds;
  in-process A/B is impossible with one module name, and drift otherwise hides 5% effects.
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

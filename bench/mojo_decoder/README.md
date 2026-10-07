# The decoder in Mojo: an experiment

`loads.mojo` is `src/decoder.c`'s untyped parser ported to Mojo, written to answer one
question: would the decoder be faster in Mojo than in C? It is not part of the package.
The C decoder is the one shipped.

## Result

No. On the same algorithm, the Mojo port decodes every corpus document to results identical
to orjson's and runs 6–9% slower than the C decoder (geometric mean over the corpus and the
1,000-record payload, three runs), within 3% on object and text documents and 10–20% behind
on arrays of numbers.

The port corresponds to the C decoder before it gained its loop for numeric arrays and the
eight-digits-at-a-time step for fractions (commit `27530ac`), which is why the gap is widest
on `numbers.json`, `mesh.json` and `canada.json`.

## Where the time went

Instruction counts under callgrind are the same for both: on `numbers.json`, 37.0M in the
Mojo number parser against 37.8M in C, 10.0M against 10.4M in the array loop. The three
differences that showed up were all idioms Mojo's LLVM backend lowered differently from GCC,
and each was fixed by writing the operation the way the C code already spelled it:

- escape decoding as comparison chains instead of a 256-byte table and a `switch`
  (twitterescaped.json went from 1.21× to 1.03× the C time);
- the four-way whitespace test lowered to an 18-instruction branchless `setcc` sequence
  where GCC emitted one bit test against a 64-bit mask;
- an unsigned-to-double conversion where the preceding range check made a signed one valid.

What remains is the backend, not the language: the same `decoder.c` built with clang-18 is
itself 3–7% slower than the GCC build on the same files, and the Mojo port is within about
3% of that clang build.

## What the compiler offers

The Mojo compiler source (`Mojo/lib/Compiler` in the Modular repository) builds a stock
LLVM O3 pipeline with no user-facing pass or backend options; `--optimization-level` is 0
or 3, and there is no PGO or LTO. Functions are non-raising by default. Two things were
worth knowing: `--bitcode-libs` links external LLVM bitcode into the module before
optimization, so C helpers compiled with `clang --target=x86_64-unknown-linux-gnu -emit-llvm`
inline into Mojo code (only with `-j 1`; the parallel build drops them silently), and
converting a `Pointer(to=local)` through an integer loses the alias link, after which the
optimizer may drop the local's initializing stores. Functions that take the parser must be
generic over the pointer's origin, as the encoder's are.

## Running it

```bash
./build.sh                                   # the package, for its layout probe
bench/mojo_decoder/build.sh                  # needs the Mojo compiler on PATH
.venv-bench/bin/python bench/mojo_decoder/compare.py build/jsonexamples
```

`compare.py` loads `loads.so` through ctypes (`PyDLL`, so the GIL stays held), checks the
results against orjson on every document and on a set of edge cases, then times the Mojo
decoder against `yjson.loads` and `orjson.loads` in alternating batches.

`powers.c` is the Eisel-Lemire power-of-five table, the same one as `src/decoder_powers.h`,
behind an accessor function because the Mojo code reads it through a pointer. The decoder
reports errors as codes with a byte position; `compare.py` turns them into `ValueError`.

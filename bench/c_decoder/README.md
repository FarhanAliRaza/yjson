# C decoding benchmark baseline

`decoder.c` is the former production `yjson.loads` implementation, including
`loads(type=...)`. It is retained for comparisons and is not built or linked by
the package build. Its generated float table is shared from `src/decoder_powers.h`.

From the repository root:

```bash
bench/c_decoder/build.sh
PYTHONPATH=bench/c_decoder:build .venv-bench/bin/python -c 'import _c_decoder; print(_c_decoder.loads(b"[1,2,3]"))'
```

`PYTHON` selects a GIL build of CPython 3.11–3.15; `CC` selects the C compiler.
The helper `_yjson_support.py` must be available on the Python import path.
The [Mojo comparison runner](../mojo_decoder/README.md) checks results before
measuring alternating batches of this baseline, shipping Mojo and orjson.

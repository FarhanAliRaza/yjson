#!/usr/bin/env bash
# Builds a standalone adapter for the production Mojo parser.
# Needs the Mojo compiler on PATH (see CONTRIBUTING.md). Targets the CPython
# selected by PYTHON (defaults to .venv-bench, then python3).
set -euo pipefail
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON:-../../.venv-bench/bin/python}"
if [ ! -x "$PYTHON_BIN" ]; then PYTHON_BIN="${PYTHON:-python3}"; fi
read -r PYTHON_INCLUDE EXT_SUFFIX < <("$PYTHON_BIN" - <<'PY'
import platform
import sys
import sysconfig
if platform.python_implementation() != "CPython" or not (3, 11) <= sys.version_info[:2] <= (3, 15):
    raise SystemExit("The benchmark parser requires CPython 3.11–3.15")
if sysconfig.get_config_var("Py_GIL_DISABLED"):
    raise SystemExit("The benchmark parser requires a GIL build of CPython")
print(sysconfig.get_path("include"), sysconfig.get_config_var("EXT_SUFFIX"))
PY
)
LAYOUT=()
# Probe the same interpreter the native entry point targets.
mkdir -p ../../build
"${CC:-cc}" -I "$PYTHON_INCLUDE" ../../src/layout_probe.c -o ../../build/layout_probe-mojo
while IFS='=' read -r name value; do LAYOUT+=(-D "$name=$value"); done < <(../../build/layout_probe-mojo)
"${CC:-cc}" -O3 -fPIC -c ../../src/decoder_powers.c -o powers.o
"${CC:-cc}" -O3 -fPIC -Wall -Wextra -Werror -I "$PYTHON_INCLUDE" -c ../../src/decoder_api.c -o decoder_api.o
"${MOJO:-mojo}" build --mcpu "${MCPU:-x86-64-v3}" "${LAYOUT[@]}" ../../src/decoder.mojo --emit shared-lib -Xlinker "$PWD/powers.o" -Xlinker "$PWD/decoder_api.o" -o loads.so
"${CC:-cc}" -O3 -fPIC -shared -Wall -Wextra -Werror -I "$PYTHON_INCLUDE" native.c -L "$PWD" -Wl,-rpath,'$ORIGIN' -l:loads.so -o "_mojo_decoder$EXT_SUFFIX"
echo "built $PWD/loads.so and _mojo_decoder$EXT_SUFFIX"

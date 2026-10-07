#!/usr/bin/env bash
# Builds the former production C parser for the selected benchmark interpreter.
set -euo pipefail
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON:-../../.venv-bench/bin/python}"
if [ ! -x "$PYTHON_BIN" ]; then PYTHON_BIN="${PYTHON:-python3}"; fi
read -r PYTHON_INCLUDE EXT_SUFFIX < <("$PYTHON_BIN" - <<'PY'
import platform, sys, sysconfig
if platform.python_implementation() != "CPython" or not (3, 11) <= sys.version_info[:2] <= (3, 15):
    raise SystemExit("The C baseline requires CPython 3.11–3.15")
if sysconfig.get_config_var("Py_GIL_DISABLED"):
    raise SystemExit("The C baseline requires a GIL build")
print(sysconfig.get_path("include"), sysconfig.get_config_var("EXT_SUFFIX"))
PY
)
"${CC:-cc}" -O3 -fPIC -shared -Wall -Wextra -Werror -I "$PYTHON_INCLUDE" -I ../../src decoder.c native.c -o "_c_decoder$EXT_SUFFIX"
echo "built $PWD/_c_decoder$EXT_SUFFIX"

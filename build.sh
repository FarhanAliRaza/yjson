#!/usr/bin/env bash
# Builds yjson for one CPython (3.11 - 3.15, default GIL build, x86-64 with AVX2).
# Needs Mojo 1.1:  pip install mojo
# The output is build/yjson<EXT_SUFFIX> (e.g. yjson.cpython-313-x86_64-linux-gnu.so),
# so builds for several interpreters can live side by side in build/.
# YJSON_DIRECT_DICT=0 makes the dict writers call PyDict_Next instead of
# walking the key table (for comparison only; the default is the direct walk).
# MCPU selects the target (default x86-64-v3 = AVX2; x86-64-v4 adds AVX-512
# string scanning). The build only runs on CPUs with the chosen features.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
PYTHON_BIN="${PYTHON:-.venv-bench/bin/python}"
if [ ! -x "$PYTHON_BIN" ]; then PYTHON_BIN="${PYTHON:-python3}"; fi
read -r PYTHON_INCLUDE EXT_SUFFIX PYTHON_TAG VERSION < <("$PYTHON_BIN" - <<'EOF'
import sys, sysconfig, tomllib
if not (3, 11) <= sys.version_info[:2] <= (3, 15):
    sys.exit(f"yjson supports CPython 3.11 through 3.15, not {sys.version.split()[0]}")
if sysconfig.get_config_var("Py_GIL_DISABLED"):
    sys.exit("yjson does not support free-threaded CPython builds")
with open("pyproject.toml", "rb") as f:
    version = tomllib.load(f)["project"]["version"]
print(sysconfig.get_path("include"), sysconfig.get_config_var("EXT_SUFFIX"), f"{sys.version_info[0]}.{sys.version_info[1]}", version)
EOF
)
CC_BIN="${CC:-cc}"
# Object layout the Mojo loops read, probed from this interpreter's headers.
"$CC_BIN" -I "$PYTHON_INCLUDE" src/layout_probe.c -o build/layout_probe
LAYOUT_C=()
LAYOUT_MOJO=()
while IFS='=' read -r name value; do
    LAYOUT_C+=("-D$name=$value")
    LAYOUT_MOJO+=(-D "$name=$value")
done < <(build/layout_probe)
LAYOUT_MOJO+=(-D "YJSON_DIRECT_DICT=${YJSON_DIRECT_DICT:-1}")
OUT="build/yjson$EXT_SUFFIX"
# yjson.__version__ comes from pyproject.toml.
"$CC_BIN" -O3 -fPIC -Wall -Wextra -Werror "${LAYOUT_C[@]}" -DYJSON_VERSION="\"$VERSION\"" -I "$PYTHON_INCLUDE" -c src/python_api.c -o "build/python_api-$PYTHON_TAG.o"
# The loads() parser is a separate C file with no dependence on the encoder.
"$CC_BIN" -O3 -fPIC -Wall -Wextra -Werror -I "$PYTHON_INCLUDE" -c src/decoder.c -o "build/decoder-$PYTHON_TAG.o"
"${MOJO:-mojo}" build --mcpu "${MCPU:-x86-64-v3}" "${LAYOUT_MOJO[@]}" src/yjson.mojo --emit shared-lib -Xlinker "$PWD/build/python_api-$PYTHON_TAG.o" -Xlinker "$PWD/build/decoder-$PYTHON_TAG.o" -o "$OUT"
cp src/_yjson_support.py build/_yjson_support.py
cp src/yjson.pyi build/yjson.pyi
echo "built $OUT for CPython $PYTHON_TAG"

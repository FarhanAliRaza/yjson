#!/usr/bin/env bash
# Builds mojson.so (CPython 3.12, x86-64 with AVX2). Needs Mojo 1.1:  pip install mojo
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
PYTHON_BIN="${PYTHON:-.venv-bench/bin/python}"
if [ ! -x "$PYTHON_BIN" ]; then PYTHON_BIN="${PYTHON:-python3.12}"; fi
PYTHON_INCLUDE="$($PYTHON_BIN -c 'import sys, sysconfig; assert sys.version_info[:2] == (3, 12), "CPython 3.12 required"; print(sysconfig.get_path("include"))')"
"${CC:-cc}" -O3 -fPIC -Wall -Wextra -Werror -I "$PYTHON_INCLUDE" -c src/python_api.c -o build/python_api.o
"${MOJO:-mojo}" build --mcpu x86-64-v3 src/mojson.mojo --emit shared-lib -Xlinker "$PWD/build/python_api.o" -o build/mojson.so
cp src/_mojson_support.py build/_mojson_support.py
cp src/mojson.pyi build/mojson.pyi
echo "built build/mojson.so"

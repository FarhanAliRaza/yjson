#!/usr/bin/env bash
# Builds the Mojo decoder experiment into bench/mojo_decoder/loads.so.
# Needs the Mojo compiler on PATH (see CONTRIBUTING.md) and a prior ./build.sh of the
# package, whose layout probe supplies the CPython object offsets.
set -euo pipefail
cd "$(dirname "$0")"
LAYOUT=()
if [ -x ../../build/layout_probe ]; then
    while IFS='=' read -r name value; do LAYOUT+=(-D "$name=$value"); done < <(../../build/layout_probe)
fi
"${CC:-cc}" -O2 -fPIC -c powers.c -o powers.o
"${MOJO:-mojo}" build --mcpu "${MCPU:-x86-64-v3}" "${LAYOUT[@]}" loads.mojo --emit shared-lib -Xlinker "$PWD/powers.o" -o loads.so
echo "built $PWD/loads.so"

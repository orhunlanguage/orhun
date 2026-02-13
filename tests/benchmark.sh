#!/usr/bin/env bash
set -euo pipefail

COMPILER="${1:-g++}"
OUTPUT="${2:-orhun_bench}"

if [[ ! -f "${OUTPUT}" ]]; then
  echo "[build] ${OUTPUT} bulunamadi, derleniyor..."
  "${COMPILER}" -std=c++17 -Wall -Wextra -pedantic \
    main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp \
    -o "${OUTPUT}"
fi

cases=(
  "tests/cases/basic_math.oh"
  "tests/cases/assignment_equals.oh"
  "tests/cases/while_float.oh"
  "tests/cases/list_comprehension.oh"
)

echo "[bench] Orhun hiz karsilastirma"
for src in "${cases[@]}"; do
  echo ""
  echo "=== ${src} ==="
  if ! "./${OUTPUT}" hiz "${src}" 40; then
    echo "[skip] ${src} benchmark atlandi (VM destek disi olabilir)."
  fi
done

#!/usr/bin/env bash
set -euo pipefail

COMPILER="${1:-g++}"
OUTPUT="${2:-orhun_test}"

echo "[1/3] Building..."
"${COMPILER}" -std=c++17 -Wall -Wextra -pedantic \
  main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp \
  -o "${OUTPUT}"

cases=(
  "tests/cases/basic_math"
  "tests/cases/oop_super"
  "tests/cases/list_comprehension"
  "tests/cases/try_break_continue"
  "tests/cases/assignment_equals"
  "tests/cases/json_parse"
  "tests/cases/f_string"
  "tests/cases/slicing"
  "tests/cases/stdlib_modules"
  "tests/cases/stdlib_database"
  "tests/cases/stdlib_regex_date"
  "tests/cases/dict_nested"
  "tests/cases/while_float"
  "tests/cases/module_stdlib"
  "tests/cases/try_catch_runtime"
  "tests/cases/f_string_escape"
  "tests/cases/vm_loop_control"
)

uname_lc="$(uname -s | tr '[:upper:]' '[:lower:]')"
if [[ "${uname_lc}" == *mingw* || "${uname_lc}" == *msys* || "${uname_lc}" == *cygwin* ]]; then
  cases+=(
    "tests/cases/ffi_kernel32"
    "tests/cases/ffi_text"
    "tests/cases/ffi_symbol"
  )
fi

failed=0
echo "[2/3] Running tests..."
for case in "${cases[@]}"; do
  src="${case}.oh"
  expected_path="${case}.expected.txt"

  actual="$("./${OUTPUT}" "${src}" 2>&1 || true)"
  expected="$(cat "${expected_path}")"

  # normalize trailing newline differences
  actual="${actual%$'\n'}"
  expected="${expected%$'\n'}"

  if [[ "${actual}" != "${expected}" ]]; then
    echo ""
    echo "[FAIL] ${src}"
    echo "Expected:"
    echo "${expected}"
    echo "Actual:"
    echo "${actual}"
    failed=1
  else
    echo "[OK] ${src}"
  fi
done

vm_cases=(
  "tests/cases/basic_math"
  "tests/cases/assignment_equals"
  "tests/cases/oop_super"
  "tests/cases/f_string"
  "tests/cases/f_string_escape"
  "tests/cases/json_parse"
  "tests/cases/dict_nested"
  "tests/cases/while_float"
  "tests/cases/list_comprehension"
  "tests/cases/try_break_continue"
  "tests/cases/try_catch_runtime"
  "tests/cases/stdlib_modules"
  "tests/cases/stdlib_database"
  "tests/cases/stdlib_regex_date"
  "tests/cases/module_stdlib"
  "tests/cases/vm_loop_control"
  "tests/cases/slicing"
  "tests/cases/vm_try_catch"
)

if [[ "${uname_lc}" == *mingw* || "${uname_lc}" == *msys* || "${uname_lc}" == *cygwin* ]]; then
  vm_cases+=(
    "tests/cases/ffi_kernel32"
    "tests/cases/ffi_text"
    "tests/cases/ffi_symbol"
  )
fi

echo "[3/4] Running strict VM subset..."
for case in "${vm_cases[@]}"; do
  src="${case}.oh"
  expected_path="${case}.expected.txt"

  actual="$("./${OUTPUT}" vm-kati "${src}" 2>&1 || true)"
  expected="$(cat "${expected_path}")"

  actual="${actual%$'\n'}"
  expected="${expected%$'\n'}"

  if [[ "${actual}" != "${expected}" ]]; then
    echo ""
    echo "[VM-FAIL] ${src}"
    echo "Expected:"
    echo "${expected}"
    echo "Actual:"
    echo "${actual}"
    failed=1
  else
    echo "[VM-OK] ${src}"
  fi
done

echo "[4/4] Result..."
if [[ "${failed}" -ne 0 ]]; then
  echo "Some tests failed."
  exit 1
fi
echo "All tests passed."

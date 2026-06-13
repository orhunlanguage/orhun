#!/usr/bin/env bash
set -euo pipefail

COMPILER="${1:-g++}"
OUTPUT="${2:-build/orhun_test}"
TEST_TIMEOUT_SECONDS="${ORHUN_TEST_TIMEOUT_SECONDS:-10}"

mkdir -p "$(dirname "${OUTPUT}")"

echo "[1/3] Building..."
"${COMPILER}" -std=c++17 -Wall -Wextra -pedantic \
  main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp \
  -o "${OUTPUT}"

uname_lc="$(uname -s | tr '[:upper:]' '[:lower:]')"
cases=()
while IFS= read -r test_case; do
  cases+=("${test_case}")
done < <(find tests/cases -maxdepth 1 -name '*.expected.txt' | sed 's#\\#/#g' | sed 's/\.expected\.txt$//' | sort)

filtered_cases=()
for case in "${cases[@]}"; do
  [[ -f "${case}.oh" ]] || continue
  if [[ "${uname_lc}" != *mingw* && "${uname_lc}" != *msys* && "${uname_lc}" != *cygwin* ]]; then
    case "${case}" in
      tests/cases/ffi_kernel32|tests/cases/ffi_text|tests/cases/ffi_symbol|tests/cases/ffi_tanimli_kernel32|tests/cases/ffi_dis_islev)
        continue
        ;;
    esac
  fi
  filtered_cases+=("${case}")
done
cases=("${filtered_cases[@]}")
strict_turkce_case="tests/cases/turkce_kati_alias"

run_orhun() {
  local src="$1"
  local actual=""
  local status=0
  if command -v timeout >/dev/null 2>&1; then
    set +e
    actual="$(timeout "${TEST_TIMEOUT_SECONDS}s" "./${OUTPUT}" "${src}" 2>&1)"
    status=$?
    set -e
    if [[ "${status}" -eq 124 || "${status}" -eq 137 ]]; then
      printf 'Hata: test zaman asimi (%ss)' "${TEST_TIMEOUT_SECONDS}"
      return 0
    fi
  else
    set +e
    actual="$("./${OUTPUT}" "${src}" 2>&1)"
    status=$?
    set -e
  fi

  printf '%s' "${actual}"
  if [[ "${status}" -ne 0 && "${status}" -ne 1 ]]; then
    if [[ -n "${actual}" ]]; then
      printf '\n'
    fi
    printf 'Hata: beklenmeyen cikis kodu (%s)' "${status}"
  fi
}

failed=0
echo "[2/3] Running tests..."
for case in "${cases[@]}"; do
  if [[ "${case}" == "${strict_turkce_case}" ]]; then
    continue
  fi
  src="${case}.oh"
  expected_path="${case}.expected.txt"

  actual="$(run_orhun "${src}")"
  expected="$(cat "${expected_path}")"

  actual="${actual//$'\r\n'/$'\n'}"
  actual="${actual//$'\r'/}"
  expected="${expected//$'\r\n'/$'\n'}"
  expected="${expected//$'\r'/}"

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

if [[ -f "${strict_turkce_case}.oh" && -f "${strict_turkce_case}.expected.txt" ]]; then
  src="${strict_turkce_case}.oh"
  expected_path="${strict_turkce_case}.expected.txt"
  actual="$(ORHUN_TURKCE_KATI=1 run_orhun "${src}")"
  expected="$(cat "${expected_path}")"
  actual="${actual//$'\r\n'/$'\n'}"
  actual="${actual//$'\r'/}"
  expected="${expected//$'\r\n'/$'\n'}"
  expected="${expected//$'\r'/}"
  actual="${actual%$'\n'}"
  expected="${expected%$'\n'}"
  if [[ "${actual}" != "${expected}" ]]; then
    echo ""
    echo "[FAIL] ${src} (ORHUN_TURKCE_KATI=1)"
    echo "Expected:"
    echo "${expected}"
    echo "Actual:"
    echo "${actual}"
    failed=1
  else
    echo "[OK] ${src} (ORHUN_TURKCE_KATI=1)"
  fi
fi

echo "[3/3] Result..."
if [[ "${failed}" -ne 0 ]]; then
  echo "Some tests failed."
  exit 1
fi
echo "All tests passed."

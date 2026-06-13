#!/usr/bin/env bash
set -euo pipefail

JSONL="${1:-build/benchmark_results.jsonl}"
MIN_P50="${2:-2.0}"
MIN_P90="${3:-1.5}"
MODE="${4:-suite}"
BASELINE_JSONL="${5:-}"
MIN_BASELINE_P50_RATIO="${6:-0}"
MIN_BASELINE_P90_RATIO="${7:-0}"

if [[ ! -f "${JSONL}" ]]; then
  echo "Benchmark dosyasi bulunamadi: ${JSONL}"
  exit 3
fi

budget_enabled=0
if [[ -n "${BASELINE_JSONL}" && ("${MIN_BASELINE_P50_RATIO}" != "0" || "${MIN_BASELINE_P90_RATIO}" != "0") ]]; then
  if [[ ! -f "${BASELINE_JSONL}" ]]; then
    echo "Benchmark baseline dosyasi bulunamadi: ${BASELINE_JSONL}"
    exit 3
  fi
  baseline_line_count="$(grep -cve '^[[:space:]]*$' "${BASELINE_JSONL}" || true)"
  if [[ "${baseline_line_count}" -le 0 ]]; then
    echo "Benchmark baseline dosyasi bos: ${BASELINE_JSONL}"
    exit 3
  fi
  budget_enabled=1
  echo "[BUDGET] Baseline aktif: ${BASELINE_JSONL} (P50>=${MIN_BASELINE_P50_RATIO}, P90>=${MIN_BASELINE_P90_RATIO})"
fi

extract_number() {
  local line="$1"
  local key="$2"
  echo "${line}" | sed -n "s/.*\"${key}\"[[:space:]]*:[[:space:]]*\([-+0-9.eE]*\).*/\1/p"
}

extract_string() {
  local line="$1"
  local key="$2"
  echo "${line}" | sed -n "s/.*\"${key}\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p"
}

gate_failed=0
infra_failed=0
line_count=0
p50_values=""
p90_values=""
p50_ratio_values=""
p90_ratio_values=""
while IFS= read -r line; do
  [[ -z "${line}" ]] && continue
  line_count=$((line_count + 1))
  dosya="$(extract_string "${line}" "dosya")"
  p50="$(extract_number "${line}" "p50_oran")"
  p90="$(extract_number "${line}" "p90_oran")"
  if [[ -z "${p50}" || -z "${p90}" ]]; then
    p50="$(extract_number "${line}" "p50_x")"
    p90="$(extract_number "${line}" "p90_x")"
  fi
  if [[ -z "${p50}" || -z "${p90}" ]]; then
    echo "[FAIL] ${dosya:-bilinmeyen} p50/p90 parse edilemedi."
    infra_failed=1
    continue
  fi
  abs_p50="$(extract_number "${line}" "p50_x")"
  abs_p90="$(extract_number "${line}" "p90_x")"
  if [[ -z "${abs_p50}" || -z "${abs_p90}" ]]; then
    echo "[FAIL] ${dosya:-bilinmeyen} hizlanma.p50_x/p90_x parse edilemedi."
    infra_failed=1
    continue
  fi
  p50_values="${p50_values}${p50}"$'\n'
  p90_values="${p90_values}${p90}"$'\n'
  if [[ "${MODE}" == "per_case" ]]; then
    ok_p50="$(awk -v a="${p50}" -v b="${MIN_P50}" 'BEGIN{print (a>=b)?1:0}')"
    ok_p90="$(awk -v a="${p90}" -v b="${MIN_P90}" 'BEGIN{print (a>=b)?1:0}')"
    if [[ "${ok_p50}" -ne 1 || "${ok_p90}" -ne 1 ]]; then
      echo "[FAIL] ${dosya} P50=${p50} P90=${p90} (hedef ${MIN_P50}/${MIN_P90})"
      gate_failed=1
    else
      echo "[OK] ${dosya} P50=${p50} P90=${p90}"
    fi
  else
    echo "[INFO] ${dosya} P50=${p50} P90=${p90}"
  fi

  if [[ "${budget_enabled}" -eq 1 ]]; then
    base_line=""
    while IFS= read -r candidate; do
      if [[ "$(extract_string "${candidate}" "dosya")" == "${dosya}" ]]; then
        base_line="${candidate}"
        break
      fi
    done < "${BASELINE_JSONL}"
    if [[ -z "${base_line}" ]]; then
      echo "[FAIL] Baseline'da case bulunamadi: ${dosya}"
      infra_failed=1
      continue
    fi
    base_p50="$(extract_number "${base_line}" "p50_x")"
    base_p90="$(extract_number "${base_line}" "p90_x")"
    if [[ -z "${base_p50}" || -z "${base_p90}" ]]; then
      echo "[FAIL] Baseline case p50_x/p90_x parse edilemedi: ${dosya}"
      infra_failed=1
      continue
    fi
    valid_base="$(awk -v a="${base_p50}" -v b="${base_p90}" 'BEGIN{print (a>0 && b>0)?1:0}')"
    if [[ "${valid_base}" -ne 1 ]]; then
      echo "[FAIL] Baseline case degerleri sifir/negatif: ${dosya}"
      infra_failed=1
      continue
    fi
    ratio_p50="$(awk -v a="${abs_p50}" -v b="${base_p50}" 'BEGIN{print a/b}')"
    ratio_p90="$(awk -v a="${abs_p90}" -v b="${base_p90}" 'BEGIN{print a/b}')"
    p50_ratio_values="${p50_ratio_values}${ratio_p50}"$'\n'
    p90_ratio_values="${p90_ratio_values}${ratio_p90}"$'\n'
    if [[ "${MODE}" == "per_case" ]]; then
      ok_ratio_p50=1
      ok_ratio_p90=1
      if [[ "${MIN_BASELINE_P50_RATIO}" != "0" ]]; then
        ok_ratio_p50="$(awk -v a="${ratio_p50}" -v b="${MIN_BASELINE_P50_RATIO}" 'BEGIN{print (a>=b)?1:0}')"
      fi
      if [[ "${MIN_BASELINE_P90_RATIO}" != "0" ]]; then
        ok_ratio_p90="$(awk -v a="${ratio_p90}" -v b="${MIN_BASELINE_P90_RATIO}" 'BEGIN{print (a>=b)?1:0}')"
      fi
      if [[ "${ok_ratio_p50}" -ne 1 || "${ok_ratio_p90}" -ne 1 ]]; then
        echo "[FAIL] ${dosya} budget orani P50=${ratio_p50} P90=${ratio_p90} (hedef ${MIN_BASELINE_P50_RATIO}/${MIN_BASELINE_P90_RATIO})"
        gate_failed=1
      else
        echo "[OK] ${dosya} budget orani P50=${ratio_p50} P90=${ratio_p90}"
      fi
    else
      echo "[BUDGET] ${dosya} oran P50=${ratio_p50} P90=${ratio_p90}"
    fi
  fi
done < "${JSONL}"

if [[ "${MODE}" == "suite" ]]; then
  suite_p50="$(printf "%s" "${p50_values}" | awk 'NF{print $1}' | sort -n | awk '{a[NR]=$1} END{if(NR==0){print 0; exit} if(NR%2==1){print a[(NR+1)/2]} else {print (a[NR/2]+a[NR/2+1])/2}}')"
  suite_p90="$(printf "%s" "${p90_values}" | awk 'NF{print $1}' | sort -n | awk '{a[NR]=$1} END{if(NR==0){print 0; exit} if(NR%2==1){print a[(NR+1)/2]} else {print (a[NR/2]+a[NR/2+1])/2}}')"
  echo "[SUITE] median P50=${suite_p50} P90=${suite_p90} (hedef ${MIN_P50}/${MIN_P90})"
  ok_suite_p50="$(awk -v a="${suite_p50}" -v b="${MIN_P50}" 'BEGIN{print (a>=b)?1:0}')"
  ok_suite_p90="$(awk -v a="${suite_p90}" -v b="${MIN_P90}" 'BEGIN{print (a>=b)?1:0}')"
  if [[ "${ok_suite_p50}" -ne 1 || "${ok_suite_p90}" -ne 1 ]]; then
    gate_failed=1
  fi
  if [[ "${budget_enabled}" -eq 1 ]]; then
    suite_ratio_p50="$(printf "%s" "${p50_ratio_values}" | awk 'NF{print $1}' | sort -n | awk '{a[NR]=$1} END{if(NR==0){print 0; exit} if(NR%2==1){print a[(NR+1)/2]} else {print (a[NR/2]+a[NR/2+1])/2}}')"
    suite_ratio_p90="$(printf "%s" "${p90_ratio_values}" | awk 'NF{print $1}' | sort -n | awk '{a[NR]=$1} END{if(NR==0){print 0; exit} if(NR%2==1){print a[(NR+1)/2]} else {print (a[NR/2]+a[NR/2+1])/2}}')"
    echo "[SUITE-BUDGET] median ratio P50=${suite_ratio_p50} P90=${suite_ratio_p90} (hedef ${MIN_BASELINE_P50_RATIO}/${MIN_BASELINE_P90_RATIO})"
    ok_suite_ratio_p50=1
    ok_suite_ratio_p90=1
    if [[ "${MIN_BASELINE_P50_RATIO}" != "0" ]]; then
      ok_suite_ratio_p50="$(awk -v a="${suite_ratio_p50}" -v b="${MIN_BASELINE_P50_RATIO}" 'BEGIN{print (a>=b)?1:0}')"
    fi
    if [[ "${MIN_BASELINE_P90_RATIO}" != "0" ]]; then
      ok_suite_ratio_p90="$(awk -v a="${suite_ratio_p90}" -v b="${MIN_BASELINE_P90_RATIO}" 'BEGIN{print (a>=b)?1:0}')"
    fi
    if [[ "${ok_suite_ratio_p50}" -ne 1 || "${ok_suite_ratio_p90}" -ne 1 ]]; then
      gate_failed=1
    fi
  fi
fi

if [[ "${line_count}" -le 0 ]]; then
  echo "Benchmark dosyasi bos: ${JSONL}"
  exit 3
fi

if [[ "${infra_failed}" -ne 0 ]]; then
  echo "Benchmark gate altyapi hatasi."
  exit 3
fi

if [[ "${gate_failed}" -ne 0 ]]; then
  echo "Benchmark gate basarisiz."
  exit 2
fi

echo "Benchmark gate gecti."

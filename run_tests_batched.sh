#!/bin/bash
# Protected batch test runner: runs one gtest suite per process with a hard timeout,
# aborts the whole run at the first GPU fault or crash so a bad shader cannot take
# the machine down through the driver stack.
#
# Usage: run_tests_batched.sh <binary> <logfile> [gtest_filter_prefix]
set -u
BIN="$1"
LOG="$2"
PREFIX="${3:-}"

cd "$(dirname "$BIN")"
: > "$LOG"

suites=$("$BIN" --gtest_list_tests 2>/dev/null | awk '/^[A-Za-z]/ && !/^Running/ {print $1}' | sed 's/\.$//' | grep "^${PREFIX}" | sort)
total=$(echo "$suites" | wc -l | tr -d ' ')
echo "suites: $total" >> "$LOG"

failed_suites=()
gpu_abort=0
i=0
for s in $suites; do
  i=$((i+1))
  out="/tmp/batch_$$_$i.log"
  # 150s hard timeout per suite via perl alarm; the suite dies, not the machine.
  if perl -e 'alarm shift; exec @ARGV' 150 "$BIN" "--gtest_filter=$s.*" > "$out" 2>&1; then
    status=ok
  else
    status=exit$?
  fi
  if grep -q "GPU Address Fault\|GPU error" "$out"; then
    echo "GPU-FAULT at suite $s -- aborting remaining suites" >> "$LOG"
    cat "$out" >> "$LOG"
    gpu_abort=1
    failed_suites+=("$s(gpu)")
    break
  fi
  nfail=$(grep -cE "^\[  FAILED  \] [A-Za-z]" "$out" | head -1)
  nfail=$((nfail / 2))
  if [ "$status" != "ok" ]; then
    echo "suite $s: $status, failed~$nfail" >> "$LOG"
    failed_suites+=("$s($status)")
    # A crashed suite poisons the driver state for later ones on macOS GL; stop here.
    if [ "$status" != "exit1" ]; then
      echo "crash at suite $s -- aborting remaining suites (driver state poisoned)" >> "$LOG"
      cat "$out" >> "$LOG"
      break
    fi
  else
    echo "suite $s: ok, failed~$nfail" >> "$LOG"
  fi
  cat "$out" >> "$LOG"
  rm -f "$out"
done

echo "=== SUMMARY ===" >> "$LOG"
echo "gpu_abort=$gpu_abort" >> "$LOG"
echo "failed_suites: ${failed_suites[*]-none}" >> "$LOG"

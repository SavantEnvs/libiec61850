#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the known-answer self-test that mayhem/build.sh built (build/selftest).
# upstream ships no C unit-test suite in this tree, so the oracle is mayhem/selftest.c: decode/encode/print
# known MMS BER values and assert the exact values and bytes.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

if [ ! -x build/selftest ]; then
  echo "build/selftest missing — mayhem/build.sh should have built it" >&2
  emit_ctrf "libiec61850-selftest" 0 1 0
  exit 1
fi

out=$(./build/selftest); rc=$?
printf '%s\n' "$out"
passed=$(printf '%s\n' "$out" | grep -c '^ok ')
failed=$(printf '%s\n' "$out" | grep -c '^FAIL ')
# a run that printed no result lines, or died before finishing, counts as a failure
[ "$rc" -eq 0 ] || failed=$(( failed + 1 ))
[ "$passed" -gt 0 ] || failed=$(( failed + 1 ))
emit_ctrf "libiec61850-selftest" "$passed" "$failed" 0

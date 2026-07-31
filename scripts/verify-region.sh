#!/usr/bin/env bash
# Build the region and run its tests, reporting the outcome from the exit codes
# rather than from anything printed.
#
# This exists because the obvious one-liner is wrong in a way that reads as
# right. A bash pipeline's status is its *last* command's, so
#
#     cmake --build "$build" 2>&1 | grep -Ei "error|warning" || true
#
# reports grep's verdict, and `|| true` discards even that: a compile error
# prints its diagnostics, the script carries on, and ctest runs the *previous*
# binaries. `set -o pipefail` does not rescue it, because `|| true` throws the
# recovered status away too. The client core found both of its harnesses doing
# this, one of them reporting twenty compile errors and twenty-one of twenty-one
# tests passing (2026-07-31); this repo had already shipped the deploy version of
# the same bug, installing a stale binary after a failed build and calling four
# regions healthy.
#
# So: build, check the status, and only then look at the log. The filter never
# decides the outcome.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
log=${HOMEWORLDZ_VERIFY_LOG:-/tmp/homeworldz-verify.log}

: > "$log"
status=0
bash "$root/scripts/build-region.sh" --test >>"$log" 2>&1 || status=$?

if ((status != 0)); then
  echo "FAILED (exit $status). Last 40 lines of $log:"
  tail -40 "$log"
  exit "$status"
fi

# Only now is the log worth reading, and only to summarize a run already known
# to have succeeded.
tests=$(grep -E "tests passed|tests failed" "$log" | tail -1 || true)
echo "verified: ${tests:-build succeeded, no test summary in the log}"
echo "full log: $log"

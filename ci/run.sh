#!/usr/bin/env bash
# Run a command; on failure publish the tail of its output as a GitHub annotation
# (annotations are readable without access to the full job log).
set +e
out=$(mktemp)
"$@" 2>&1 | tee "$out"
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then
  msg=$(tail -n 70 "$out" | sed 's/\x1b\[[0-9;]*m//g' | python3 -c "import sys; print(sys.stdin.read().replace('%','%25').replace('\r','').replace('\n','%0A'))")
  echo "::error title=step failed (exit $rc)::$msg"
fi
exit "$rc"

#!/bin/bash
# Run a Lens Studio CLI command safely: answers "N" to plugin permission prompts,
# enforces a timeout, and kills only the process it started.
# usage: tools/ls_cli.sh <timeout_s> <logfile> <lens studio args...>
TIMEOUT=$1; LOG=$2; shift 2
LS="/Applications/Lens Studio.app/Contents/MacOS/Lens Studio"
(yes N 2>/dev/null || true) | "$LS" "$@" > "$LOG" 2>&1 &
PIPE_PID=$!
LS_PID=$(pgrep -n -f "Lens Studio.app/Contents/MacOS/Lens Studio $1" || true)
for ((i = 0; i < TIMEOUT; i++)); do
  kill -0 $PIPE_PID 2>/dev/null || { wait $PIPE_PID; exit $?; }
  # Runaway guard: a permission prompt loop floods the log.
  if [ "$(stat -f %z "$LOG" 2>/dev/null || echo 0)" -gt 50000000 ]; then echo "ls_cli: log flood, aborting"; break; fi
  sleep 1
done
echo "ls_cli: timeout/abort after ${i}s: $*" >&2
[ -n "$LS_PID" ] && kill "$LS_PID" 2>/dev/null; sleep 2; [ -n "$LS_PID" ] && kill -9 "$LS_PID" 2>/dev/null
exit 124

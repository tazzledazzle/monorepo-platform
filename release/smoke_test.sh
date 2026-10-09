#!/usr/bin/env bash
# End-to-end smoke test: engine -> gateway -> ingest agent -> report CLI.
# Loopback only, ephemeral ports (--port=0), no HOME, everything under TEST_TMPDIR.
set -euo pipefail

# --- begin runfiles.bash initialization v3 ---
set +e
f=bazel_tools/tools/bash/runfiles/runfiles.bash
source "${RUNFILES_DIR:-/dev/null}/$f" 2>/dev/null ||
  source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -f2- -d' ')" 2>/dev/null ||
  source "$0.runfiles/$f" 2>/dev/null ||
  source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null ||
  source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null ||
  { echo >&2 "ERROR: cannot find $f"; exit 1; }
set -e
# --- end runfiles.bash initialization v3 ---

WAIT_SECONDS=60
WORK="${TEST_TMPDIR:?TEST_TMPDIR must be set}/smoke"
mkdir -p "$WORK"

ENGINE="$(rlocation _main/engine/service/engine)"
GATEWAY="$(rlocation _main/server/gateway/gateway)"
AGENT="$(rlocation _main/ingest/agent/main/agent_/agent)"
CLI="$(rlocation _main/reports/cli/cli)"
CSV="$(rlocation _main/testdata/sales.csv)"
GOLDEN="$(rlocation _main/testdata/sales_by_region.golden)"
for v in ENGINE GATEWAY AGENT CLI CSV GOLDEN; do
  [[ -n "${!v}" && -e "${!v}" ]] || { echo "FAIL: runfile for $v not found" >&2; exit 1; }
done

ENGINE_PID=""
GATEWAY_PID=""

dump_logs() {
  local f
  for f in engine.out engine.err gateway.out gateway.err; do
    echo "----- $f -----" >&2
    cat "$WORK/$f" >&2 2>/dev/null || true
  done
}

stop_pid() {
  local pid="$1"
  [[ -n "$pid" ]] || return 0
  kill "$pid" 2>/dev/null || return 0
  local i
  for i in $(seq 1 50); do
    kill -0 "$pid" 2>/dev/null || { wait "$pid" 2>/dev/null || true; return 0; }
    sleep 0.1
  done
  kill -9 "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

cleanup() {
  local rc=$?
  trap - EXIT
  stop_pid "$GATEWAY_PID"
  stop_pid "$ENGINE_PID"
  if [[ $rc -ne 0 ]]; then
    dump_logs
  fi
  exit "$rc"
}
trap cleanup EXIT
trap 'exit 143' INT TERM

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# wait_listening <pid> <stdout-file> <name>: prints the port from `LISTENING <port>`.
wait_listening() {
  local pid="$1" out="$2" name="$3" i port
  for i in $(seq 1 $((WAIT_SECONDS * 10))); do
    port="$(sed -n 's/^LISTENING \([0-9][0-9]*\)$/\1/p' "$out" 2>/dev/null | head -n1)"
    if [[ -n "$port" ]]; then
      echo "$port"
      return 0
    fi
    kill -0 "$pid" 2>/dev/null || fail "$name exited before printing LISTENING"
    sleep 0.1
  done
  fail "$name did not print LISTENING within ${WAIT_SECONDS}s"
}

"$ENGINE" --port=0 --host=127.0.0.1 >"$WORK/engine.out" 2>"$WORK/engine.err" &
ENGINE_PID=$!
ENGINE_PORT="$(wait_listening "$ENGINE_PID" "$WORK/engine.out" engine)"
echo "engine on 127.0.0.1:$ENGINE_PORT"

"$GATEWAY" --port=0 --engine="127.0.0.1:$ENGINE_PORT" >"$WORK/gateway.out" 2>"$WORK/gateway.err" &
GATEWAY_PID=$!
GATEWAY_PORT="$(wait_listening "$GATEWAY_PID" "$WORK/gateway.out" gateway)"
echo "gateway on 127.0.0.1:$GATEWAY_PORT"

# --- happy path: ingest, query, diff against the golden file ---
"$AGENT" --engine="127.0.0.1:$ENGINE_PORT" --dataset=sales "$CSV" >"$WORK/agent.out" 2>"$WORK/agent.err" ||
  { cat "$WORK/agent.err" >&2; fail "agent exited non-zero"; }
grep -qx 'loaded 10 rows into sales' "$WORK/agent.out" ||
  { cat "$WORK/agent.out" >&2; fail "agent did not report 'loaded 10 rows into sales'"; }

"$CLI" --gateway="127.0.0.1:$GATEWAY_PORT" --dataset=sales --agg=sum:revenue --group-by=region \
  >"$WORK/report.out" 2>"$WORK/report.err" ||
  { cat "$WORK/report.err" >&2; fail "cli exited non-zero on the happy path"; }
if ! diff -u "$GOLDEN" "$WORK/report.out" >&2; then
  fail "CLI stdout differs from testdata/sales_by_region.golden"
fi
echo "golden report matches"

# --- error paths: one-line stderr, exit code 1, nothing on stdout ---
# expect_cli_error <expected status name> <cli args...>
expect_cli_error() {
  local want="$1" rc=0 lines
  shift
  "$CLI" --gateway="127.0.0.1:$GATEWAY_PORT" "$@" >"$WORK/err.out" 2>"$WORK/err.err" || rc=$?
  [[ $rc -eq 1 ]] || fail "$want case: expected exit code 1, got $rc (stderr: $(cat "$WORK/err.err"))"
  lines="$(wc -l <"$WORK/err.err" | tr -d ' ')"
  [[ "$lines" -eq 1 ]] || fail "$want case: expected exactly one stderr line, got $lines: $(cat "$WORK/err.err")"
  grep -q "$want" "$WORK/err.err" || fail "$want case: stderr lacks $want: $(cat "$WORK/err.err")"
  [[ ! -s "$WORK/err.out" ]] || fail "$want case: unexpected stdout: $(cat "$WORK/err.out")"
  echo "$want case ok: $(cat "$WORK/err.err")"
}

expect_cli_error NOT_FOUND --dataset=no_such_dataset --agg=sum:revenue
expect_cli_error INVALID_ARGUMENT --dataset=sales --agg=sum:no_such_column

echo "PASS"

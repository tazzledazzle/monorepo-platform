#!/usr/bin/env bash
# Negative-test runner: every case under tests/negative/ declares an expected outcome and
# this script verifies it. Exits 0 only if every case behaves as declared.
#
# Run from anywhere:   scripts/check_negative.sh
# Options:             -v   print the captured bazel output of failing cases
#
# Why a script and not a bazel sh_test: visibility and loading errors surface while BUILD
# files load/analyse, so they cannot be asserted from inside the build graph, and running
# bazel inside bazel would deadlock on the server lock. Cases are tagged `manual` so
# `bazel build //...` stays green. The script reuses the existing output base (no clean),
# needs no terminal (--color=no --curses=no --noshow_progress), and prints one PASS/FAIL
# line per case plus a one-line summary.
#
# Case kinds (see run_case calls below):
#   build_fail  `bazel build <target>` must fail and its output must contain <text>
#   build_pass  `bazel build <target>` must succeed
#   query_fail  `bazel query <target>` (loading phase only) must fail and contain <text>
#
# Loading-error fixtures (tests/negative/loading_fixtures/*.BUILD) are not named BUILD.bazel
# because a package that fails to load would break `bazel build //...` even when tagged
# manual. The script stages each into a throwaway package tests/negative/_staged_<case>/
# for the duration of the run and removes it on exit (do not run //... concurrently).

set -u
cd "$(dirname "$0")/.." || exit 2

VERBOSE=0
[[ "${1:-}" == "-v" ]] && VERBOSE=1

BAZEL="${BAZEL:-bazel}"
FLAGS=(--color=no --curses=no --noshow_progress)
STAGED=()
cleanup() { for d in "${STAGED[@]:-}"; do [[ -n "$d" ]] && rm -rf "$d"; done; }
trap cleanup EXIT
stage() { # stage <fixture name>; sets STAGED_LABEL (no subshell, so cleanup sees it)
  local d="tests/negative/_staged_$1"
  mkdir -p "$d" && cp "tests/negative/loading_fixtures/$1.BUILD" "$d/BUILD.bazel" && STAGED+=("$d")
  STAGED_LABEL="//$d"
}

pass=0
fail=0
start=$SECONDS

# run_case <name> <kind> <target> [expected text]
run_case() {
  local name="$1" kind="$2" target="$3" text="${4:-}" out rc verdict=""
  case "$kind" in
    build_fail|build_pass) out=$("$BAZEL" build "${FLAGS[@]}" "$target" 2>&1 </dev/null); rc=$? ;;
    query_fail)            out=$("$BAZEL" query "${FLAGS[@]}" "$target" 2>&1 </dev/null); rc=$? ;;
    *) echo "FAIL  $name: unknown kind $kind"; fail=$((fail + 1)); return ;;
  esac
  case "$kind" in
    build_pass)
      [[ $rc -eq 0 ]] || verdict="expected success, bazel exited $rc" ;;
    *)
      if [[ $rc -eq 0 ]]; then
        verdict="expected failure, but bazel succeeded"
      elif ! grep -qF -- "$text" <<<"$out"; then
        verdict="failed (rc=$rc) but output lacks expected text: $text"
      fi ;;
  esac
  if [[ -z "$verdict" ]]; then
    echo "PASS  $name  [${kind#*_}${text:+: $text}]"
    pass=$((pass + 1))
  else
    echo "FAIL  $name  $verdict"
    [[ $VERBOSE -eq 1 ]] && sed 's/^/        | /' <<<"$out" | tail -30
    fail=$((fail + 1))
  fi
}

N=//tests/negative

# (a) visibility
run_case vis-engine-core    build_fail $N/vis_engine:breach "is not visible from"
run_case vis-server-catalog build_fail $N/vis_server:breach "is not visible from"
# (b) -Werror / lint
run_case werror-cc-unused-variable build_fail $N/werror_cc:unused_var "unused variable 'x' [-Werror,-Wunused-variable]"
run_case werror-java-rawtypes      build_fail $N/werror_java:lint     "[rawtypes]"
run_case werror-kt-warnings        build_fail $N/werror_kt:warns      "warnings found and -Werror specified"
# (c) legacy leak vs symbolic macro privacy
run_case legacy-hdrs-leak          build_pass $N/legacy_leak:uses_legacy_helper
run_case modern-helper-private     build_fail $N/modern_helper_breach:uses_macro_helper "is not visible from"
# (d) loading errors
stage missing_team
run_case missing-team-attr    query_fail "$STAGED_LABEL:all" "missing value for mandatory attribute 'team'"
stage grpc_go
run_case grpc-service-lang-go query_fail "$STAGED_LABEL:all" "invalid value in 'lang' attribute"

echo "check_negative: $pass passed, $fail failed in $((SECONDS - start))s"
[[ $fail -eq 0 ]]

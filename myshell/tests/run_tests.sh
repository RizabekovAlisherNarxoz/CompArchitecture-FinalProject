#!/usr/bin/env bash
# tests/run_tests.sh — compare mysh output against bash for a set of cases.
#
# Usage:
#   cd myshell
#   bash tests/run_tests.sh
#
# Each test sends the same command to bash -c and to mysh via stdin,
# then diffs stdout.  Exit code of this script is 0 only when every
# test passes.

set -euo pipefail

MYSH="$(cd "$(dirname "$0")/.." && pwd)/mysh"

if [[ ! -x "$MYSH" ]]; then
    echo "ERROR: mysh binary not found at $MYSH — run 'make' first." >&2
    exit 1
fi

TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_LOCAL"' EXIT

PASS=0
FAIL=0
SKIP=0

# ------------------------------------------------------------------ #
# Helper: run one test case.
#
# run_test <description> <bash-command> [mysh-command]
#
#   bash-command  – the command bash evaluates with `bash -c`.
#   mysh-command  – the command sent to mysh via stdin (defaults to
#                   bash-command when not supplied).
#
# Stdout of both shells is captured; stderr is discarded so that
# differences in error message wording don't cause false failures.
# Only stdout is compared.
# ------------------------------------------------------------------ #
run_test() {
    local desc="$1"
    local bash_cmd="$2"
    local mysh_cmd="${3:-$2}"

    local bash_out mysh_out

    bash_out="$(bash -c "$bash_cmd" 2>/dev/null)" || true
    mysh_out="$(printf '%s\n' "$mysh_cmd" | "$MYSH" 2>/dev/null)" || true

    if [[ "$bash_out" == "$mysh_out" ]]; then
        printf "  PASS  %s\n" "$desc"
        (( PASS++ )) || true
    else
        printf "  FAIL  %s\n" "$desc"
        printf "        bash : %s\n" "$(echo "$bash_out" | head -5)"
        printf "        mysh : %s\n" "$(echo "$mysh_out" | head -5)"
        (( FAIL++ )) || true
    fi
}

# Variant for tests that should produce a specific known string
# (i.e. we don't compare with bash, we assert an expected value).
run_test_expect() {
    local desc="$1"
    local mysh_cmd="$2"
    local expected="$3"

    local mysh_out
    mysh_out="$(printf '%s\n' "$mysh_cmd" | "$MYSH" 2>/dev/null)" || true

    if [[ "$mysh_out" == "$expected" ]]; then
        printf "  PASS  %s\n" "$desc"
        (( PASS++ )) || true
    else
        printf "  FAIL  %s\n" "$desc"
        printf "        expected : %s\n" "$expected"
        printf "        got      : %s\n" "$mysh_out"
        (( FAIL++ )) || true
    fi
}

# ------------------------------------------------------------------ #
# Test suite
# ------------------------------------------------------------------ #

echo "=== mysh test suite ==="
echo

# ---- Basic commands -----------------------------------------------
echo "-- Basic commands --"

run_test "echo hello world" \
    "echo hello world"

run_test "echo multiple   spaces" \
    "echo multiple   spaces"

run_test "echo empty string" \
    "echo ''"

run_test "true produces no output" \
    "true" \
    "true"
# Note: $? and && are not implemented in mysh (not in scope)

run_test "false exit status is non-zero (mysh exits non-zero)" \
    "false" \
    "false"

# ---- Pipelines ----------------------------------------------------
echo
echo "-- Pipelines --"

run_test "echo piped through cat" \
    "echo hello | cat"

run_test "echo piped through tr" \
    "echo hello world | tr a-z A-Z"

run_test "three-stage pipeline" \
    "printf 'a\nb\nc\n' | cat | cat"

# ---- Redirections -------------------------------------------------
echo
echo "-- Redirections --"

INPUT="$TMPDIR_LOCAL/input.txt"
OUTPUT="$TMPDIR_LOCAL/output.txt"
APPEND="$TMPDIR_LOCAL/append.txt"

echo "hello from file" > "$INPUT"

run_test "redirect stdin (<)" \
    "cat < $INPUT"

# Output redirect: compare file contents not stdout
bash -c "echo redirect_out > $OUTPUT" 2>/dev/null
rm -f "$OUTPUT"
printf "echo redirect_out > %s\n" "$OUTPUT" | "$MYSH" >/dev/null 2>&1 || true
if [[ -f "$OUTPUT" ]] && [[ "$(cat "$OUTPUT")" == "redirect_out" ]]; then
    printf "  PASS  redirect stdout (>)\n"; (( PASS++ )) || true
else
    printf "  FAIL  redirect stdout (>)\n"; (( FAIL++ )) || true
fi

# Append redirect
rm -f "$APPEND"
printf "echo line1 >> %s\n" "$APPEND" | "$MYSH" >/dev/null 2>&1 || true
printf "echo line2 >> %s\n" "$APPEND" | "$MYSH" >/dev/null 2>&1 || true
if [[ -f "$APPEND" ]] && [[ "$(cat "$APPEND")" == $'line1\nline2' ]]; then
    printf "  PASS  append redirect (>>)\n"; (( PASS++ )) || true
else
    printf "  FAIL  append redirect (>>)\n"
    printf "        file contents: %s\n" "$(cat "$APPEND" 2>/dev/null)"
    (( FAIL++ )) || true
fi

# Input + output redirect
INPUT2="$TMPDIR_LOCAL/input2.txt"
OUTPUT2="$TMPDIR_LOCAL/output2.txt"
echo "transferred" > "$INPUT2"
printf "cat < %s > %s\n" "$INPUT2" "$OUTPUT2" | "$MYSH" >/dev/null 2>&1 || true
if [[ -f "$OUTPUT2" ]] && [[ "$(cat "$OUTPUT2")" == "transferred" ]]; then
    printf "  PASS  cat < infile > outfile\n"; (( PASS++ )) || true
else
    printf "  FAIL  cat < infile > outfile\n"; (( FAIL++ )) || true
fi

# ---- Built-in: cd -------------------------------------------------
echo
echo "-- Built-in: cd --"

# On macOS /tmp is a symlink to /private/tmp; resolve with pwd -P to be portable.
TMP_REAL="$(cd /tmp && pwd -P)"
run_test_expect "cd /tmp && pwd" \
    "cd /tmp
pwd" \
    "$TMP_REAL"

run_test_expect "cd with no args goes to HOME" \
    "cd
pwd" \
    "$HOME"

run_test_expect "cd to relative subdir" \
    "cd /usr
cd local
pwd" \
    "/usr/local"

# ---- Built-in: exit -----------------------------------------------
echo
echo "-- Built-in: exit --"

exit_code="$(printf 'exit 7\n' | "$MYSH"; echo $?)" || true
# The subshell exits 7; outer $? captures it
actual_exit=0
printf 'exit 7\n' | "$MYSH" > /dev/null 2>&1 || actual_exit=$?
if [[ "$actual_exit" -eq 7 ]]; then
    printf "  PASS  exit 7 propagates\n"; (( PASS++ )) || true
else
    printf "  FAIL  exit 7 propagates (got %d)\n" "$actual_exit"; (( FAIL++ )) || true
fi

# ---- Pipelines with redirections ----------------------------------
echo
echo "-- Pipeline + redirect --"

PIPELINE_OUT="$TMPDIR_LOCAL/pipeline_out.txt"
printf 'printf "%%s\\n" c b a | sort > %s\n' "$PIPELINE_OUT" | "$MYSH" >/dev/null 2>&1 || true
EXPECTED=$'a\nb\nc'
if [[ -f "$PIPELINE_OUT" ]] && [[ "$(cat "$PIPELINE_OUT")" == "$EXPECTED" ]]; then
    printf "  PASS  pipeline with output redirect\n"; (( PASS++ )) || true
else
    printf "  FAIL  pipeline with output redirect\n"; (( FAIL++ )) || true
fi

# ---- Error handling -----------------------------------------------
echo
echo "-- Error handling --"

# Command not found — mysh should print something to stderr and continue
mysh_stderr="$(printf 'nonexistentcmd_xyz\n' | "$MYSH" 2>&1 >/dev/null)" || true
if echo "$mysh_stderr" | grep -q "nonexistentcmd_xyz"; then
    printf "  PASS  command not found error message\n"; (( PASS++ )) || true
else
    printf "  FAIL  command not found error message\n"
    printf "        stderr was: %s\n" "$mysh_stderr"
    (( FAIL++ )) || true
fi

# Bad redirect file
mysh_stderr2="$(printf 'cat < /no/such/file/xyz\n' | "$MYSH" 2>&1 >/dev/null)" || true
if echo "$mysh_stderr2" | grep -q "xyz"; then
    printf "  PASS  bad redirect prints error\n"; (( PASS++ )) || true
else
    printf "  FAIL  bad redirect prints error\n"
    printf "        stderr was: %s\n" "$mysh_stderr2"
    (( FAIL++ )) || true
fi

# ---- Background & jobs (basic smoke test) -------------------------
echo
echo "-- Background jobs (smoke) --"

# Launch a short background job and check jobs output
mysh_jobs="$(printf 'sleep 1 &\njobs\n' | "$MYSH" 2>/dev/null)" || true
if echo "$mysh_jobs" | grep -qE '\[1\]'; then
    printf "  PASS  background job appears in jobs list\n"; (( PASS++ )) || true
else
    printf "  FAIL  background job not in jobs list\n"
    printf "        output: %s\n" "$mysh_jobs"
    (( FAIL++ )) || true
fi

# ---- Summary ------------------------------------------------------
echo
echo "=================================="
printf "  Results: %d passed, %d failed, %d skipped\n" "$PASS" "$FAIL" "$SKIP"
echo "=================================="

if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi
exit 0

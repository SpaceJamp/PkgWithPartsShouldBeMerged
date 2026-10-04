#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# PkgWithPartsShouldBeMerged - local bug-check gate (POSIX: macOS / Linux)
#
# Runs the passes that can be checked locally and prints a summary. Exits
# non-zero if any pass fails.
#
#   ./tools/check_all.sh [path/to/pkg_merge]
#
# Pass 7 (foreign platforms, i.e. does the OTHER platform build) is not covered
# here - only CI can judge that. See AGENTS.md.
#
# SPDX-License-Identifier: GPL-3.0-only. See LICENSE for the licence text.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

EXE="${1:-build/pkg_merge}"
FAILED=0

pass()  { printf '  \033[32mok  \033[0m %s %s\n' "$1" "${2:-}"; }
fail()  { printf '  \033[31mFAIL\033[0m %s %s\n' "$1" "${2:-}"; FAILED=$((FAILED + 1)); }
section() { printf '\n\033[36m--- %s ---\033[0m\n' "$1"; }

# --- Pass 1: build -----------------------------------------------------------
section "pass 1 build"
if command -v cmake >/dev/null 2>&1; then
  if cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPKG_MERGE_WERROR=ON >/dev/null 2>&1 \
     && cmake --build build --parallel >/dev/null 2>&1; then
    pass "build Release (/W4 /WX equivalents)"
  else
    fail "build Release"
  fi
  if [ -x build/pkg_merge ]; then EXE=build/pkg_merge; fi
else
  fail "cmake not found"
fi

# --- Pass 3: test-harness integrity ------------------------------------------
section "pass 3 test-harness integrity"
for script in tests/smoke_test.sh tools/check_all.sh; do
  if [ ! -f "$script" ]; then
    fail "$script is missing"
  elif bash -n "$script" 2>/dev/null; then
    pass "$script parses"
  else
    fail "$script has a syntax error"
  fi
done
for script in tests/smoke_test.sh tools/check_all.sh; do
  if [ -f "$script" ] && [ ! -x "$script" ]; then
    fail "$script is not executable (git: git update-index --chmod=+x $script)"
  fi
done
# GNU-only constructs that break on macOS.
if grep -q 'head -c' tests/smoke_test.sh 2>/dev/null; then
  fail "tests/smoke_test.sh uses 'head -c', which macOS does not support"
fi
assertions=$(grep -c 'assert_' tests/smoke_test.sh)
if [ "$assertions" -ge 40 ]; then
  pass "smoke suite has assertions ($assertions)"
else
  fail "smoke suite has too few assertions ($assertions)"
fi

# --- Pass 4/5: behaviour and robustness --------------------------------------
section "pass 4 behaviour"
if [ -x "$EXE" ]; then
  smoke_output="$(bash tests/smoke_test.sh "$EXE" 2>&1)"
  smoke_code=$?
  if [ "$smoke_code" -ne 0 ]; then
    printf '%s\n' "$smoke_output" | grep -E '\[FAIL\]|FAIL -' || true
    fail "smoke test (exit $smoke_code)"
    # A failing suite short-circuits assert_merged_content, so the check count
    # is lower for a reason that says nothing about coverage.
    printf '       (smoke coverage not assessed: the suite failed)\n'
  else
    pass "smoke test"
    # Coverage must not silently shrink: 60 checks are expected on POSIX.
    reported="$(printf '%s' "$smoke_output" | sed -n 's/.*[^0-9]\([0-9]*\) total.*/\1/p' | tail -1)"
    if [ -z "$reported" ]; then
      reported="$(printf '%s' "$smoke_output" | sed -n 's/.*PASS - \([0-9]*\) checks succeeded.*/\1/p' | tail -1)"
    fi
    if [ -n "$reported" ] && [ "$reported" -ge 60 ]; then
      pass "smoke coverage ($reported checks, expected >= 60)"
    else
      fail "smoke coverage reported '${reported:-none}', expected >= 60"
    fi
  fi
else
  fail "executable not found or not runnable: $EXE"
fi

# --- Summary -----------------------------------------------------------------
printf '\n\033[36m================ summary ================\033[0m\n'
if [ "$FAILED" -eq 0 ]; then
  printf '\033[32mLOCAL GATE PASSED\033[0m\n'
  printf 'pass 7 (foreign platforms) is not covered here - check the CI run.\n'
  exit 0
fi
printf '\033[31mLOCAL GATE FAILED (%d checks)\033[0m\n' "$FAILED"
exit 1
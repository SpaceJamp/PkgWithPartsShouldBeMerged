#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# PkgWithPartsShouldBeMerged robustness test - POSIX (Linux)
#
# Mirrors tests/robustness_test.ps1. Without this, Linux - a shipping target -
# was covered only by the smoke suite: every hostile case below ran on Windows
# alone, which is exactly where a POSIX-specific bug would hide. Permissions,
# rename() over an existing file, O_EXCL and EINTR all behave differently from
# Win32 and this program has separate branches for each.
#
# Each case must produce a clear diagnostic, a non-zero exit code where it
# should fail, and must not leave a partial or temporary file behind.
#
# Usage: ./tests/robustness_test.sh [path/to/pkg_merge]
#
# SPDX-License-Identifier: GPL-3.0-only. See LICENSE for the licence text.
set -uo pipefail

EXE="${1:-./build/pkg_merge}"
WORK_ROOT="${TMPDIR:-/tmp}/PkgWithPartsShouldBeMerged-hostile-$$"
CHECKS=0
FAILURES=0
SKIPPED=0
LARGE=1

while [ "$#" -gt 0 ]; do
  case "$1" in
    --skip-large) LARGE=0 ;;
    *) printf 'unknown option %s\n' "$1" >&2; exit 2 ;;
  esac
  shift
done

pass() { CHECKS=$((CHECKS + 1)); printf '  \033[32m[ok]\033[0m   %s\n' "$1"; }
fail() { CHECKS=$((CHECKS + 1)); FAILURES=$((FAILURES + 1)); printf '  \033[31m[FAIL]\033[0m %s\n' "$1"; }
assert_true() { if [ "$1" = "0" ]; then pass "$2"; else fail "$2"; fi; }
assert_eq() { if [ "$1" = "$2" ]; then pass "$3 (expected '$1', got '$2')"; else fail "$3 (expected '$1', got '$2')"; fi; }
section() { printf '\n\033[36m%s\033[0m\n' "$1"; }
# A check this platform or this machine cannot run still counts towards the
# total, so the coverage figure does not depend on the host.
skip_check() { CHECKS=$((CHECKS + 1)); SKIPPED=$((SKIPPED + 1)); printf '  \033[33m[skip]\033[0m %s\n' "$1"; }

reset_dir() { rm -rf "$1"; mkdir -p "$1"; }
chmod_tree() { chmod -R "$1" "$2" 2>/dev/null || true; }

make_part() {
  local path="$1" bytes="$2" seed="$3" magic="${4:-no}"
  awk -v n="$bytes" -v s="$seed" \
    'BEGIN { srand(s); for (i = 0; i < n; i++) printf "%c", 97 + int(rand() * 26) }' > "$path"
  if [ "$magic" = "magic" ]; then
    printf '\177CNT' | dd of="$path" bs=1 count=4 conv=notrunc 2>/dev/null
  fi
}

make_game() {
  local dir="$1" title="$2" count="$3" size="${4:-8192}"
  local i
  for i in $(seq 0 $((count - 1))); do
    local magic="no"
    [ "$i" -eq 0 ] && magic="magic"
    make_part "$dir/${title}_${i}.pkg" $((size + i)) $((1000 + i)) "$magic"
  done
}

run_tool() {
  local output
  output="$("$EXE" "$@" 2>&1)"
  EXIT_CODE=$?
  OUTPUT="$output"
  return 0
}

# SPEC 7.1 and 18.2: no temporary file may survive, and its name is the
# implementer's choice. Compare against a snapshot taken before the run rather
# than matching a name. A merged output is the only expected addition.
assert_no_leftovers() {
  local dir="$1" what="$2" before="$3"
  local after stray
  after="$(mktemp)"
  find "$dir" -type f 2>/dev/null | sort > "$after"
  stray="$(comm -13 "$before" "$after" | grep -v -- '-merged\.pkg$' | wc -l | tr -d ' ')"
  rm -f "$after"
  assert_eq 0 "$stray" "$what leaves nothing behind but its merged output"
}

snapshot() { find "$1" -type f 2>/dev/null | sort; }

printf '\033[36mPkgWithPartsShouldBeMerged robustness test (POSIX)\033[0m\n'
printf '  exe: %s\n' "$EXE"
[ -x "$EXE" ] || { printf 'executable not found or not runnable: %s\n' "$EXE" >&2; exit 2; }
mkdir -p "$WORK_ROOT"

# --- 1. filenames that are not pieces ---------------------------------------
section "[1] hostile filenames"
odd="$WORK_ROOT/names"; reset_dir "$odd"
make_part "$odd/_0.pkg" 1024 1 magic
make_part "$odd/no-number.pkg" 1024 1 magic
make_part "$odd/trailing_.pkg" 1024 1 magic
make_part "$odd/_0.pkg.txt" 1024 1 magic
make_part "$odd/CUSA00001_99999999999999999999.pkg" 1024 1
make_part "$odd/CUSA00001_-1.pkg" 1024 1
make_part "$odd/.pkg" 512 1
make_part "$odd/CUSA00002_0.pkg" 4096 1 magic
make_part "$odd/CUSA00002_1.pkg" 4096 2
snap=$(snapshot "$odd")
run_tool -i "$odd" --overwrite
# SPEC 4: a name that is not a piece file is ignored silently, so this succeeds -
# only CUSA00002 is a real set.
assert_true "$([ "$EXIT_CODE" -eq 0 ] && echo 0 || echo 1)" "junk filenames are ignored silently and the valid set still merges (exit $EXIT_CODE)"
if [ -f "$odd/CUSA00002-merged.pkg" ]; then r=0; else r=1; fi
assert_true "$r" "the one valid game in the folder still merges"
if [ -f "$odd/_0-merged.pkg" ]; then r=1; else r=0; fi
assert_true "$r" "a nameless piece never becomes a merged file"
assert_no_leftovers "$odd" "hostile filenames" "$snap"

# --- 2. mixed case titles group together ------------------------------------
section "[2] case insensitive titles"
casedir="$WORK_ROOT/case"; reset_dir "$casedir"
make_part "$casedir/CUSA00003_0.pkg" 8192 1 magic
make_part "$casedir/cusa00003_1.pkg" 8192 2
snap=$(snapshot "$casedir")
run_tool -i "$casedir" --overwrite --verify
assert_eq 0 "$EXIT_CODE" "CUSA00003_0 and cusa00003_1 merge as one game"
assert_eq 16384 "$(wc -c < "$casedir/CUSA00003-merged.pkg" 2>/dev/null || echo -1)" "mixed case merge has both pieces"
assert_no_leftovers "$casedir" "mixed case titles" "$snap"

# --- 3. dots and spaces in the title ----------------------------------------
section "[3] title with dots and spaces"
dotted="$WORK_ROOT/dotted"; reset_dir "$dotted"
make_part "$dotted/Game Name v1.0_0.pkg" 8192 1 magic
make_part "$dotted/Game Name v1.0_1.pkg" 8192 2
snap=$(snapshot "$dotted")
run_tool -i "$dotted" --overwrite --verify
assert_eq 0 "$EXIT_CODE" "a dotted title parses"
# SPEC 7: the output name is the upper-case form, so this is case-sensitively
# exact here - unlike on Windows, where the filesystem would match either.
assert_eq 16384 "$(wc -c < "$dotted/GAME NAME V1.0-merged.pkg" 2>/dev/null || echo -1)" "dotted title merged both pieces, named in upper case"

# --- 4. a long sequence of pieces -------------------------------------------
section "[4] 200 pieces in order"
many="$WORK_ROOT/many"; reset_dir "$many"
i=0
while [ "$i" -lt 200 ]; do
  magic=no; [ "$i" -eq 0 ] && magic=magic
  make_part "$many/CUSA00004_$i.pkg" 1024 $((100 + i)) "$magic"
  i=$((i + 1))
done
snap=$(snapshot "$many")
run_tool -i "$many" --overwrite --verify --quiet
assert_eq 0 "$EXIT_CODE" "200 pieces merge successfully"
assert_eq 204800 "$(wc -c < "$many/CUSA00004-merged.pkg" 2>/dev/null || echo -1)" "all 200 pieces landed in order"
assert_no_leftovers "$many" "200 pieces" "$snap"

# --- 5. pieces supplied out of order ----------------------------------------
section "[5] shuffled piece order on disk"
shuffled="$WORK_ROOT/shuffled"; reset_dir "$shuffled"
make_part "$shuffled/CUSA00005_3.pkg" 1024 3
make_part "$shuffled/CUSA00005_0.pkg" 1024 1 magic
make_part "$shuffled/CUSA00005_1.pkg" 1024 2
make_part "$shuffled/CUSA00005_2.pkg" 1024 4
run_tool -i "$shuffled" --overwrite --verify --quiet
assert_eq 0 "$EXIT_CODE" "shuffled input merges successfully"
assert_eq 4096 "$(wc -c < "$shuffled/CUSA00005-merged.pkg" 2>/dev/null || echo -1)" "shuffled merge has the right size"

# --- 6. unwritable output directory -----------------------------------------
section "[6] unwritable output directory"
ro_in="$WORK_ROOT/readonly-in"; reset_dir "$ro_in"
make_part "$ro_in/CUSA00006_0.pkg" 4096 1 magic
make_part "$ro_in/CUSA00006_1.pkg" 4096 2
ro_out="$WORK_ROOT/readonly-out"; reset_dir "$ro_out"
snap=$(snapshot "$ro_in")
chmod 555 "$ro_out"
run_tool -i "$ro_in" -o "$ro_out" --overwrite
chmod 755 "$ro_out"
if [ "$EXIT_CODE" -ne 0 ]; then r=0; else r=1; fi
assert_true "$r" "an unwritable output directory fails (exit $EXIT_CODE)"
case "$OUTPUT" in *readonly-out*) r=0;; *) r=1;; esac
assert_true "$r" "the unwritable output directory is named"
assert_no_leftovers "$ro_in" "unwritable output directory" "$snap"

# --- 7. an unreadable source file -------------------------------------------
section "[7] unreadable source file"
lock_in="$WORK_ROOT/locked"; reset_dir "$lock_in"
make_part "$lock_in/CUSA00007_0.pkg" 65536 1 magic
make_part "$lock_in/CUSA00007_1.pkg" 65536 2
snap=$(snapshot "$lock_in")
chmod 000 "$lock_in/CUSA00007_1.pkg"
if [ "$(id -u)" = "0" ]; then
  # root reads a mode 000 file happily, so this cannot be provoked here.
  chmod 644 "$lock_in/CUSA00007_1.pkg"
  skip_check "an unreadable source file fails"
  skip_check "the unreadable file is named"
else
  run_tool -i "$lock_in" --overwrite
  chmod 644 "$lock_in/CUSA00007_1.pkg"
  if [ "$EXIT_CODE" -ne 0 ]; then r=0; else r=1; fi
  assert_true "$r" "an unreadable source file fails (exit $EXIT_CODE)"
  case "$OUTPUT" in *CUSA00007_1.pkg*) r=0;; *) r=1;; esac
  assert_true "$r" "the unreadable file is named"
fi
assert_no_leftovers "$lock_in" "unreadable source file" "$snap"

# --- 8. an unlistable sub-directory -----------------------------------------
section "[8] unlistable sub-directory"
hidden="$WORK_ROOT/hidden"; reset_dir "$hidden"
sub="$hidden/sub"; mkdir -p "$sub"
make_part "$sub/CUSA00008_0.pkg" 4096 1 magic
make_part "$sub/CUSA00008_1.pkg" 4096 2
snap=$(snapshot "$hidden")
chmod 000 "$sub"
run_tool -i "$hidden" --overwrite
chmod 755 "$sub"
# SPEC 12: an entry that cannot be read is skipped, not fatal.
assert_eq 0 "$EXIT_CODE" "an unlistable sub-directory does not abort the scan"

# --- 9. more than 2 GiB ------------------------------------------------------
section "[9] more than 2 GiB (64-bit size regression)"
if [ "$LARGE" -eq 0 ]; then
  printf '  \033[33m[skip]\033[0m >2 GiB - skipped on request\n'
else
  big="$WORK_ROOT/big"; reset_dir "$big"
  # Sparse files, so this costs little disk but still exercises 64-bit offsets.
  each=$((1280 * 1024 * 1024))
  for index in 0 1; do
    path="$big/CUSA00009_$index.pkg"
    dd if=/dev/zero of="$path" bs=1 count=4 seek=0 conv=notrunc 2>/dev/null
    printf '\177CNT' | dd of="$path" bs=1 count=4 conv=notrunc 2>/dev/null
    truncate -s "$each" "$path"
  done
  snap=$(snapshot "$big")
  run_tool -i "$big" --overwrite --verify --quiet
  assert_eq 0 "$EXIT_CODE" "a 2.5 GiB merge succeeds"
  assert_eq $((each * 2)) "$(wc -c < "$big/CUSA00009-merged.pkg" 2>/dev/null || echo -1)" "the merged file is exactly $((each * 2)) bytes"
  assert_no_leftovers "$big" "2.5 GiB merge" "$snap"
  rm -rf "$big"
fi

chmod_tree 755 "$WORK_ROOT"
rm -rf "$WORK_ROOT"

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
  passed=$((CHECKS - SKIPPED))
  if [ "$SKIPPED" -gt 0 ]; then
    printf '\033[32mPASS - %s succeeded, %s skipped, %s total\033[0m\n' "$passed" "$SKIPPED" "$CHECKS"
  else
    printf '\033[32mPASS - %s total\033[0m\n' "$CHECKS"
  fi
  exit 0
fi
printf '\033[31mFAIL - %s of %s checks failed\033[0m\n' "$FAILURES" "$CHECKS"
exit 1
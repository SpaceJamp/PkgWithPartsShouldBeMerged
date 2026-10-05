#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# PkgWithPartsShouldBeMerged smoke test - POSIX (macOS / Linux)
#
# Mirrors tests/smoke_test.ps1. The folder dialog is skipped by always passing
# an explicit input directory.
#
# Usage: ./tests/smoke_test.sh [path/to/pkg_merge]
#
# SPDX-License-Identifier: GPL-3.0-only. See LICENSE for the licence text.
set -uo pipefail

EXE="${1:-./build/pkg_merge}"
WORK_ROOT="${TMPDIR:-/tmp}/PkgWithPartsShouldBeMerged-smoke-$$"
CHECKS=0
FAILURES=0

pass() { CHECKS=$((CHECKS + 1)); printf '  \033[32m[ok]\033[0m   %s\n' "$1"; }
fail() { CHECKS=$((CHECKS + 1)); FAILURES=$((FAILURES + 1)); printf '  \033[31m[FAIL]\033[0m %s\n' "$1"; }
assert_true() { if [ "$1" = "0" ]; then pass "$2"; else fail "$2"; fi; }
assert_eq() { if [ "$1" = "$2" ]; then pass "$3 (expected '$1', got '$2')"; else fail "$3 (expected '$1', got '$2')"; fi; }
section() { printf '\n\033[36m%s\033[0m\n' "$1"; }

reset_dir() { rm -rf "$1"; mkdir -p "$1"; }

# Deterministic filler so the merged output can be compared byte for byte.
# Only the length and determinism matter (the suite compares the merge against
# the concatenation of the sources), so plain ASCII is used to avoid any
# encoding ambiguity. Written with awk because BSD/macOS "head" has no -c.
make_part() {
  local path="$1" bytes="$2" seed="$3" magic="${4:-no}"
  awk -v n="$bytes" -v s="$seed" \
    'BEGIN { srand(s); for (i = 0; i < n; i++) printf "%c", 97 + int(rand() * 26) }' > "$path"
  if [ "$magic" = "magic" ]; then
    printf '\177CNT' | dd of="$path" bs=1 count=4 conv=notrunc 2>/dev/null
  fi
}

make_game() {
  local dir="$1" title="$2" count="$3" size="${4:-65536}"
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

# Verifies that <out>/<title>-merged.pkg equals the concatenation of its pieces.
assert_merged_content() {
  local out_dir="$1" title="$2"; shift 2
  local merged="$out_dir/${title}-merged.pkg"
  if [ ! -f "$merged" ]; then
    fail "$title-merged.pkg was created"
    return
  fi
  pass "$title-merged.pkg was created"
  local expected
  expected="$(cat "$@" | cksum)"
  local actual
  actual="$(cksum < "$merged")"
  assert_eq "$expected" "$actual" "$title-merged.pkg is byte identical to its pieces"
}

printf '\033[36mPkgWithPartsShouldBeMerged smoke test\033[0m\n'
printf '  exe: %s\n' "$EXE"
[ -x "$EXE" ] || { echo "executable not found or not runnable: $EXE" >&2; exit 2; }
assert_true 0 "executable exists"

# --- 1. help and version ------------------------------------------------------
section "[1] help and version"
run_tool --help;                   assert_eq 0 "$EXIT_CODE" "--help exits with 0"
case "$OUTPUT" in *--input*) r=0;; *) r=1;; esac;        assert_true "$r" "--help documents --input"
case "$OUTPUT" in *--no-clobber*) r=0;; *) r=1;; esac;   assert_true "$r" "--help documents --no-clobber"
run_tool --version;                assert_eq 0 "$EXIT_CODE" "--version exits with 0"

# --- 2. usage errors ---------------------------------------------------------
section "[2] usage errors"
run_tool --definitely-not-an-option; assert_eq 1 "$EXIT_CODE" "unknown option exits with 1"
case "$OUTPUT" in *definitely-not-an-option*) r=0;; *) r=1;; esac; assert_true "$r" "the unknown option is named"
run_tool --input;                    assert_eq 1 "$EXIT_CODE" "missing option value exits with 1"

# --- 3. bad input path -------------------------------------------------------
section "[3] bad input path"
bad_in="$WORK_ROOT/none"; reset_dir "$bad_in"
make_part "$bad_in/not-a-folder.pkg" 1024 7 magic
run_tool -i "$bad_in/not-a-folder.pkg"; assert_eq 1 "$EXIT_CODE" "input that is a file exits with 1"
case "$OUTPUT" in *not-a-folder.pkg*) r=0;; *) r=1;; esac; assert_true "$r" "the offending path is named"

empty_in="$WORK_ROOT/empty"; reset_dir "$empty_in"
run_tool -i "$empty_in";                     assert_eq 1 "$EXIT_CODE" "empty folder exits with 1"
run_tool -i "$empty_in" -o "$WORK_ROOT/nope"; assert_eq 1 "$EXIT_CODE" "missing output directory exits with 1"

# --- 4. two games in one folder (the old key-collision bug) -------------------
section "[4] two games sharing part numbers"
games="$WORK_ROOT/games"; reset_dir "$games"
out_dir="$WORK_ROOT/out";  reset_dir "$out_dir"
make_game "$games" CUSA11111 3
make_game "$games" CUSA22222 2
echo "not a pkg" > "$games/readme.txt"
run_tool -i "$games" -o "$out_dir" --verify --quiet
assert_eq 0 "$EXIT_CODE" "merging two games exits with 0"
assert_merged_content "$out_dir" CUSA11111 "$games"/CUSA11111_*.pkg
assert_merged_content "$out_dir" CUSA22222 "$games"/CUSA22222_*.pkg

# --- 5. positional arguments -------------------------------------------------
section "[5] positional input and output"
pos_in="$WORK_ROOT/pos-in";  reset_dir "$pos_in"
pos_out="$WORK_ROOT/pos-out"; reset_dir "$pos_out"
make_game "$pos_in" CUSA33333 2
run_tool "$pos_in" "$pos_out"
assert_eq 0 "$EXIT_CODE" "positional arguments exit with 0"
assert_merged_content "$pos_out" CUSA33333 "$pos_in"/CUSA33333_*.pkg

# --- 6. dry run --------------------------------------------------------------
section "[6] dry run"
dry_in="$WORK_ROOT/dry-in"; reset_dir "$dry_in"
make_game "$dry_in" CUSA44444 2
run_tool -i "$dry_in" --dry-run --overwrite
assert_eq 0 "$EXIT_CODE" "--dry-run exits with 0"
if [ -f "$dry_in/CUSA44444-merged.pkg" ]; then r=1; else r=0; fi
assert_true "$r" "--dry-run wrote nothing"

# --- 7. gap in the sequence --------------------------------------------------
section "[7] missing piece"
gap_in="$WORK_ROOT/gap-in"; reset_dir "$gap_in"
make_part "$gap_in/CUSA55555_0.pkg" 1024 1 magic
make_part "$gap_in/CUSA55555_2.pkg" 1024 2
run_tool -i "$gap_in" --overwrite
assert_eq 1 "$EXIT_CODE" "gap in the part sequence fails"
# SPEC 6.2: the message must name the first missing number, which is 1 here.
if printf '%s' "$OUTPUT" | grep -q 'CUSA55555' && printf '%s' "$OUTPUT" | grep -qE '(^|[^0-9])1([^0-9]|$)'; then r=0; else r=1; fi
assert_true "$r" "the diagnostic names the set and the missing number 1"
if [ -f "$gap_in/CUSA55555-merged.pkg" ]; then r=1; else r=0; fi
assert_true "$r" "no output written on a gap"

# --- 8. pieces without a root ------------------------------------------------
section "[8] pieces without a root piece"
orphan_in="$WORK_ROOT/orphan-in"; reset_dir "$orphan_in"
make_part "$orphan_in/CUSA66666_1.pkg" 1024 4
make_part "$orphan_in/CUSA66666_2.pkg" 1024 5
run_tool -i "$orphan_in" --overwrite
assert_eq 1 "$EXIT_CODE" "missing root piece fails instead of crashing"
# SPEC 6.1: the message must name the missing file name.
case "$OUTPUT" in *CUSA66666_0.pkg*) r=0;; *) r=1;; esac; assert_true "$r" "the missing root piece's file name is named"

# --- 9. duplicate part number ------------------------------------------------
section "[9] duplicate part number"
dup_in="$WORK_ROOT/dup-in"; reset_dir "$dup_in"
make_game "$dup_in" CUSA77777 2
make_part "$dup_in/CUSA77777_01.pkg" 1024 9
run_tool -i "$dup_in" --overwrite
assert_eq 1 "$EXIT_CODE" "duplicate part number fails"
# SPEC 6.3: the message must name both file names.
dup_a=1; dup_b=1
case "$OUTPUT" in *CUSA77777_1.pkg*) dup_a=0;; esac
case "$OUTPUT" in *CUSA77777_01.pkg*) dup_b=0;; esac
if [ "$dup_a" -eq 0 ] && [ "$dup_b" -eq 0 ]; then r=0; else r=1; fi
assert_true "$r" "both duplicate file names are named"

# --- 10. empty piece ---------------------------------------------------------
section "[10] empty piece"
empty_piece="$WORK_ROOT/emptypiece-in"; reset_dir "$empty_piece"
make_part "$empty_piece/CUSA88888_0.pkg" 1024 6 magic
: > "$empty_piece/CUSA88888_1.pkg"
run_tool -i "$empty_piece" --overwrite
assert_eq 1 "$EXIT_CODE" "an empty piece fails"
# SPEC 6.4: the message must name the offending file.
case "$OUTPUT" in *CUSA88888_1.pkg*) r=0;; *) r=1;; esac; assert_true "$r" "the empty piece's file name is named"

# --- 11. overwrite / no-clobber ---------------------------------------------
section "[11] overwrite policy"
over_in="$WORK_ROOT/over-in"; reset_dir "$over_in"
make_game "$over_in" CUSA99999 2
run_tool -i "$over_in" --overwrite
merged_path="$over_in/CUSA99999-merged.pkg"
first_hash="$(cksum < "$merged_path")"
run_tool -i "$over_in" --no-clobber
assert_eq 0 "$EXIT_CODE" "--no-clobber exits with 0"
case "$OUTPUT" in *CUSA99999-merged.pkg*) r=0;; *) r=1;; esac; assert_true "$r" "--no-clobber names the file it skipped"
assert_eq "$first_hash" "$(cksum < "$merged_path")" "--no-clobber left the existing file untouched"
run_tool -i "$over_in" --no-clobber --overwrite
assert_eq 1 "$EXIT_CODE" "--no-clobber with --overwrite is rejected"

run_tool -i "$over_in"   # non-interactive: keeps the existing file
assert_eq "$first_hash" "$(cksum < "$merged_path")" "a non-interactive run keeps the existing file"
case "$OUTPUT" in *CUSA99999-merged.pkg*) r=0;; *) r=1;; esac; assert_true "$r" "the kept file is named in the non-interactive skip"
run_tool -i "$over_in" --overwrite --verify
assert_eq 0 "$EXIT_CODE" "--overwrite replaces the file"

# --- 12. verify --------------------------------------------------------------
section "[12] verification"
verify_in="$WORK_ROOT/verify-in"; reset_dir "$verify_in"
make_game "$verify_in" CUSA13131 2
run_tool -i "$verify_in" --overwrite --verify
assert_eq 0 "$EXIT_CODE" "--verify succeeds on a good merge"
case "$OUTPUT" in *CUSA13131-merged.pkg*) r=0;; *) r=1;; esac; assert_true "$r" "--verify names the file it verified"

# --- 13. recursive scan ------------------------------------------------------
section "[13] recursive scan"
rec_in="$WORK_ROOT/rec-in"; reset_dir "$rec_in"
rec_sub="$rec_in/sub"; mkdir -p "$rec_sub"
make_game "$rec_sub" CUSA14141 2
run_tool -i "$rec_in" --overwrite
assert_eq 1 "$EXIT_CODE" "a flat scan that finds nothing fails"
if [ -n "$(printf '%s' "$OUTPUT" | tr -d '[:space:]')" ]; then r=0; else r=1; fi
assert_true "$r" "the empty scan produces a diagnostic"
case "$OUTPUT" in *rec-in*) r=0;; *) r=1;; esac; assert_true "$r" "the diagnostic names the folder that was scanned"
run_tool -i "$rec_in" -r --overwrite --verify
assert_eq 0 "$EXIT_CODE" "recursive scan exits with 0"
# The output directory defaults to the *input* directory.
assert_merged_content "$rec_in" CUSA14141 "$rec_sub"/CUSA14141_*.pkg

# --- 14. non-ASCII path ------------------------------------------------------
section "[14] non-ASCII path"
uni_in="$WORK_ROOT/Ünïcödé 日本語 🎮/in"; reset_dir "$uni_in"
uni_out="$WORK_ROOT/Ünïcödé 日本語 🎮/out"; reset_dir "$uni_out"
make_game "$uni_in" CUSA15151 2
run_tool -i "$uni_in" -o "$uni_out" --overwrite --verify
assert_eq 0 "$EXIT_CODE" "non-ASCII paths exit with 0"
assert_merged_content "$uni_out" CUSA15151 "$uni_in"/CUSA15151_*.pkg

# --- 15. idempotence ---------------------------------------------------------
section "[15] re-running is idempotent"
idem_in="$WORK_ROOT/idem-in"; reset_dir "$idem_in"
make_game "$idem_in" CUSA16161 2
# Snapshot after the fixture exists, before the tool ever runs.
# SPEC 7.1 and 18.2: no temporary file may be left behind, and the temporary
# file's name is the implementer's choice. A before/after comparison is naming
# independent; filtering on "is not a .pkg" would be wrong, because a fixture may
# legitimately contain files with other extensions.
idem_list="$(mktemp)"
find "$idem_in" -type f | sort > "$idem_list"
run_tool -i "$idem_in" --overwrite
run_tool -i "$idem_in" --overwrite --verify
assert_eq 0 "$EXIT_CODE" "a second run succeeds"
assert_merged_content "$idem_in" CUSA16161 "$idem_in"/CUSA16161_[0-9].pkg
count="$(find "$idem_in" -name '*-merged.pkg' | wc -l | tr -d ' ')"
assert_eq 1 "$count" "only one merged file exists"
stray="$(comm -13 "$idem_list" <(find "$idem_in" -type f | sort) |
  grep -v -- '-merged\.pkg$' | wc -l | tr -d ' ')"
rm -f "$idem_list"
assert_eq 0 "$stray" "no temporary files are left behind"

# --- 16. numeric ordering past piece 9 ----------------------------------------
# SPEC 5 and 18.3: ordering is by piece number, not by file name. Past piece 9 a
# string sort puts _10 before _2 and produces a corrupt file with no error, so
# this needs 12 pieces and a byte-exact comparison.
#
# The source list is assembled in numeric order deliberately. A glob such as
# "$ord_in"/CUSA17171_*.pkg expands lexicographically (_0 _1 _10 _11 _2 ...),
# which is the exact order this test exists to detect, and the expected checksum
# would then be computed from it.
section "[16] numeric ordering past piece 9"
ord_in="$WORK_ROOT/ord-in"; reset_dir "$ord_in"
make_game "$ord_in" CUSA17171 12
ord_args=""
i=0
while [ "$i" -lt 12 ]; do
  ord_args="$ord_args $ord_in/CUSA17171_$i.pkg"
  i=$((i + 1))
done
run_tool -i "$ord_in" --overwrite --verify --quiet
assert_eq 0 "$EXIT_CODE" "a 12-piece set merges"
# shellcheck disable=SC2086  # word splitting is wanted: one path per piece
assert_merged_content "$ord_in" CUSA17171 $ord_args

# --- 17. root piece without the PKG magic (SPEC 6.5) --------------------------
# SPEC 6.5 is a warning only and must never prevent a merge. Every other fixture
# puts the magic on its root piece.
section "[17] root piece without the PKG magic"
nomagic_in="$WORK_ROOT/nomagic-in"; reset_dir "$nomagic_in"
make_part "$nomagic_in/CUSA18181_0.pkg" 2048 31
make_part "$nomagic_in/CUSA18181_1.pkg" 4096 32
run_tool -i "$nomagic_in" --overwrite --verify --quiet
assert_eq 0 "$EXIT_CODE" "a root piece without the PKG magic still merges"
assert_merged_content "$nomagic_in" CUSA18181 \
  "$nomagic_in/CUSA18181_0.pkg" "$nomagic_in/CUSA18181_1.pkg"

# --- 18. backup, JSON output and build metadata -------------------------------
section "[18] backup, JSON and version metadata"
bk_in="$WORK_ROOT/backup-in"; reset_dir "$bk_in"
make_game "$bk_in" CUSA19191 2
run_tool -i "$bk_in" --overwrite
bk_merged="$bk_in/CUSA19191-merged.pkg"
bk_first="$(cksum < "$bk_merged")"
# Different bytes, so the re-merge cannot produce the same output by accident.
make_part "$bk_in/CUSA19191_1.pkg" 40000 41
run_tool -i "$bk_in" --overwrite --backup
assert_eq 0 "$EXIT_CODE" "a merge with --backup succeeds"
kept="$(find "$bk_in" -maxdepth 1 -name 'CUSA19191-merged.pkg*' ! -name 'CUSA19191-merged.pkg' | wc -l | tr -d ' ')"
assert_eq 1 "$kept" "--backup kept exactly one copy of the previous output"
assert_eq "$bk_first" "$(cksum < "$(find "$bk_in" -maxdepth 1 -name 'CUSA19191-merged.pkg*' ! -name 'CUSA19191-merged.pkg' | head -1)")" "the kept copy is the previous output, byte for byte"
if [ "$(cksum < "$bk_merged")" = "$bk_first" ]; then r=1; else r=0; fi
assert_true "$r" "the new output really did replace it"

# --json has to be the only thing on stdout, or a caller cannot parse it. There
# is no JSON parser guaranteed here, so check the shape structurally.
js_in="$WORK_ROOT/json-in"; reset_dir "$js_in"
make_game "$js_in" CUSA20202 2
run_tool -i "$js_in" --overwrite --json
assert_eq 0 "$EXIT_CODE" "a --json run succeeds"
case "$OUTPUT" in '{'*) r=0;; *) r=1;; esac
assert_true "$r" "--json output begins with an object"
if printf '%s' "$OUTPUT" | tr -d ' \n\t' | grep -q '"counts":{"merged":1,"skipped":0,"failed":0}'; then r=0; else r=1; fi
assert_true "$r" "--json reports the merged, skipped and failed counts"
if [ "$(printf '%s' "$OUTPUT" | grep -c '"titleId"')" -eq 1 ]; then r=0; else r=1; fi
assert_true "$r" "--json reports one entry per set"

run_tool --version
if printf '%s' "$OUTPUT" | grep -qE '\([0-9a-f]{7,}\)'; then r=0; else r=1; fi
assert_true "$r" "--version reports the commit it was built from"

# SPEC 10 and 14: the summary is informational, so --quiet suppresses it. This
# was ambiguous in the specification until it was pinned there; pin it here too.
q_in="$WORK_ROOT/quiet-in"; reset_dir "$q_in"
make_game "$q_in" CUSA21212 2
run_tool -i "$q_in" --overwrite --quiet
assert_eq 0 "$EXIT_CODE" "--quiet still merges"
assert_eq "" "$(printf '%s' "$OUTPUT" | tr -d '[:space:]')" "--quiet prints no informational output, not even the summary"
if [ -f "$q_in/CUSA21212-merged.pkg" ]; then r=0; else r=1; fi
assert_true "$r" "--quiet still wrote the output"

rm -rf "$WORK_ROOT"

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
  printf '\033[32mPASS - %d checks succeeded\033[0m\n' "$CHECKS"
  exit 0
fi
printf '\033[31mFAIL - %d of %d checks failed\033[0m\n' "$FAILURES" "$CHECKS"
exit 1
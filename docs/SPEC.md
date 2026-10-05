<!-- SPDX-License-Identifier: GPL-3.0-only -->
# PkgWithPartsShouldBeMerged — behavioural specification

## 1. Purpose

Reassemble a large PS4 `.pkg` file that was distributed as numbered byte-range
pieces back into a single file that can be installed.

A piece set is a sequence of files named `<TITLE_ID>_<N>.pkg`, where `N` starts at
`0`. Piece `0` contains the container header; every piece is a contiguous slice of
the original file's byte stream. Concatenating them in ascending `N` order
reproduces the original file exactly.

## 2. Scope

**In scope**

- Scanning one directory (optionally recursing) for piece sets.
- Grouping pieces into sets by title identifier.
- Validating a set before writing anything.
- Concatenating a validated set into a single output file.
- Reporting progress, and cancelling on request.

**Out of scope**

- Parsing, validating or installing the PKG container itself. The output is the
  concatenation; whether it is a usable PKG is not this program's concern beyond
  the header check in §6.
- Splitting, repacking, patching or signing PKGs.
- Anything requiring administrator privileges.

## 3. Terminology

| Term | Meaning |
| --- | --- |
| **Piece file** | A file matching `<TITLE_ID>_<N>.pkg` |
| **Title identifier** | The text before the final `_<N>`; an identifier such as `CUSA12345` |
| **Piece number** | `N`, a non-negative decimal integer |
| **Set** | All piece files in the scanned scope that share one title identifier |
| **Root piece** | The member of a set whose piece number is `0` |
| **Output file** | The merged result for one set |
| **Scan scope** | The input directory, plus sub-directories when recursion is enabled |

## 4. Inputs

- A directory that exists and is readable.
- Optionally a separate output directory, which must also exist.
- Piece files are recognised **only** when their name matches, case-insensitively,
  the regular expression `^(.+)_(\d+)\.pkg$`.

Anything else in the scan scope is not a piece file and must be ignored
silently, including files this program produced previously.

### 4.1 Case handling

Title identifiers are compared case-insensitively and normalised to upper case
for output naming. `CUSA12345_0.pkg` and `cusa12345_1.pkg` therefore belong to one
set.

Case folding covers ASCII. Two identifiers that differ only in the case of a
non-ASCII letter are therefore *different* sets, each with its own output file.
That is a known limitation rather than a decision: on Windows the filesystem
treats such names as equal, so both sets cannot even be created there, and on a
case-sensitive filesystem the two output names are genuinely distinct files.
PS4 title identifiers are ASCII in practice.

### 4.2 Ambiguous names

The identifier group must be non-empty, so `_0.pkg` is not a piece file. Piece
numbers are matched as decimal digits only; a sign, whitespace or any other
character means the name is not a piece file. Piece numbers are unbounded in
principle; a number too large to represent must cause the name to be rejected,
not to wrap or be clamped.

## 5. Ordering

Pieces are concatenated in ascending piece-number order, regardless of the order
the file system returns them in. Ordering must be by numeric piece number, never
by string comparison, so piece `10` follows piece `9`.

## 6. Validation

Each set must pass all of the following **before any output is written**. A
failure in any of 6.1–6.4 aborts that set with a non-zero result; other sets in
the same scan are unaffected.

| # | Rule | Failure message must name |
| --- | --- | --- |
| 6.1 | A root piece (number `0`) is present | the missing file name |
| 6.2 | Piece numbers form an unbroken run `1..N` with no gaps | the first missing number |
| 6.3 | No piece number appears twice | both file names |
| 6.4 | Every piece is readable and at least one byte long | the offending file |
| 6.5 | The root piece begins with the container magic bytes `7F 43 4E 54` | — warning only, merge proceeds |

Rule 6.5 is a warning because alternative container variants exist; it must never
prevent a merge.

Rules 6.1 and 6.2 are the two that matter most: they are what stops a corrupt
output being produced and labelled successful.

## 7. Output

- Path: `<output directory>/<TITLE ID>-merged.pkg`, where the title identifier is
  the normalised (upper-case) form.
- If no output directory was given, output goes in the input directory.
- **Source pieces are never modified, renamed or deleted.**
- If the output file already exists, the overwrite policy of §9 applies.

### 7.1 Atomicity

Output must be written to a temporary file in the destination directory and moved
into place only after the result is verified. A cancelled, failed or interrupted
run must never leave a partially written file under the output name, and must
not leave its temporary file behind.

The move must be atomic where the platform permits it.

### 7.2 Verification before publishing

Before the temporary file is promoted, the number of bytes written must equal the
sum of the input sizes. A mismatch discards the result and reports failure.

## 8. Resource handling

- Memory use must be bounded and independent of file size. Large pieces must be
  streamed in fixed-size blocks; the program must not load a whole piece into
  memory and must not attempt to map a whole file.
- All size arithmetic must be capable of representing sizes beyond 4 GiB. A build
  with a 32-bit address space must still handle pieces larger than its own
  address space, because nothing is mapped wholesale.
- File handles must be released on every exit path, including error paths.

## 9. Overwrite policy

When the output file already exists:

- `overwrite` mode: replace it without asking.
- `no-clobber` mode: leave it, skip the set, and report the skip.
- Otherwise: if a terminal is interactive, ask the user to confirm, defaulting to
  **no**; if no terminal is available, leave the file and skip, reporting that
  the existing file was kept.

An existing output file must never be deleted without one of those decisions.

## 10. Command-line interface

```
PkgWithPartsShouldBeMerged [OPTIONS] [INPUT_DIR [OUTPUT_DIR]]
```

| Option | Meaning |
| --- | --- |
| `-i`, `--input DIR` | Input directory. If omitted, prompt with a native folder dialog. |
| `-o`, `--output DIR` | Output directory. Defaults to the input directory. |
| `-r`, `--recursive` | Include sub-directories in the scan. |
| `-f`, `--overwrite` | Overwrite existing output without asking. |
| `--no-clobber` | Never overwrite; skip existing output. |
| `--backup` | When replacing an existing output, move it aside first instead of destroying it. |
| `--verify` | Re-read the output and compare it byte for byte against the sources. **On by default**; `--no-verify` skips the extra read pass. |
| `--no-verify` | Do not re-read and compare the result. |
| `--json` | Print the result as a single JSON object on standard output, and nothing else on that stream, so it can be parsed directly. |
| `-n`, `--dry-run` | Report what would be merged; write nothing. |
| `-q`, `--quiet` | Suppress progress and informational output; keep warnings and errors. |
| `--no-pause` | Do not wait for a key press before exiting. |
| `-h`, `--help` | Print usage and exit successfully. |
| `-v`, `--version` | Print the version and exit successfully. |

Requirements:

- Options may appear in any order; the first two positional arguments are the
  input and output directories.
- `--` ends option parsing.
- An unknown option, or an option missing its value, is an error: print the
  problem plus the usage text, and exit non-zero.
- `--overwrite` together with `--no-clobber` is an error.
- The program must be usable by drag-and-drop: a folder path passed as an argument
  behaves exactly like `--input`.
- When no input is given, the folder dialog is used if one is available. If no
  dialog is available - no GUI session, or no picker tool installed - the program
  reports that no input folder was given and that `--input` is required, and
  exits non-zero. It must never hang waiting for a dialog that cannot appear.

## 11. Progress and cancellation

- While merging, report percentage, bytes done of total, throughput and estimated
  time remaining, updating at most roughly ten times per second.
- On an interactive terminal, progress is rewritten in place; when output is
  redirected, progress is emitted as discrete lines.
- Cancellation is available at any point during a merge, via the platform's
  interrupt signal and via the Escape key on Windows. On cancellation the
  temporary file is removed and the program reports that it was cancelled.

## 12. Error handling

- Every failure is reported to the user as a readable message naming the file or
  set involved. The program must never terminate via an unhandled exception or an
  abort.
- Failures that concern one set must not prevent other valid sets in the same scan
  from merging.
- Directory entries that cannot be read (for example a sub-directory the user
  cannot list) must be skipped without aborting the scan.
- Disk space is checked against the required total before a merge starts.

## 13. Exit codes

| Code | Condition |
| --- | --- |
| `0` | Everything merged, or nothing to do because every set was skipped |
| `1` | Nothing merged: bad arguments, unusable input, the scan found no piece sets at all, or every set failed validation |
| `2` | Some sets merged and at least one failed |
| `130` | Cancelled on request |

The two cases that both merge nothing are deliberately different. A scan that
found piece sets but kept every existing output is a successful no-op and exits
`0`. A scan that found **no piece sets at all** — an empty folder, or one whose
pieces all sit in sub-directories when recursion was not requested — exits `1`,
because naming the wrong folder is a usage error rather than a finished job.

## 14. Output format

- Human-readable lines on standard output.
- A summary line reporting counts of merged, skipped and failed sets. The
  summary is informational, so `--quiet` suppresses it along with the progress
  line; warnings and errors are never suppressed.
- A final success line when at least one set merged and none failed.
- Non-ASCII file names must be displayed correctly, not mangled. On Windows this
  requires reading arguments as UTF-16 and writing console output as UTF-8, and
  converting paths losslessly for display.

## 15. Platform requirements

Targets are **Windows (x86 and x86-64)** and **Linux (x86-64)**, from one source
tree. macOS is not a target.

- **The project must contain no third-party code.** Every dependency must be
  either the platform's own API or the C++ standard library.
- **Windows:** the folder dialog uses the platform's own file-picker API
  (`IFileDialog`), directly.
- **Linux:** the folder dialog is obtained by invoking `zenity` or `kdialog`,
  whichever is found first. Neither is a build-time dependency. If neither is
  present, or the invocation fails, the program must **not** fail: it reports
  that no folder dialog is available and instructs the user to pass `--input`.
  A Linux installation with no GUI tooling must still be able to use the program
  from a terminal.
- Paths longer than 260 characters must work.
- When launched by double-click, the console window must remain open until the
  user acknowledges, unless `--no-pause` is given.

## 16. Build requirements

- The default configuration is optimised and free of assertions. A debug
  configuration must never be what gets released.
- The Windows executable must be self-contained, with no separately installed C++
  runtime. On Linux, linking the system C++ runtime is acceptable.
- Compiling on Linux must require no GUI or toolkit development packages.
- Compile with warnings enabled and treat them as errors in CI.
- System-level exploit mitigations must be enabled and verifiable: address-space
  layout randomisation, non-executable stack, and control-flow guard. On a
  32-bit Windows build, high-entropy address-space randomisation is unavailable
  by design and must not be claimed.
- Emit a version resource and an application manifest on Windows, declaring no
  elevation requirement and awareness of long paths.

## 17. Verification criteria

An implementation is correct when:

1. Concatenating the members of a valid set in ascending piece number order
   yields a file byte-identical to the concatenation of the sources.
2. Each validation rule in §6 has a test that fails when the rule is violated, and
   produces a clear message naming the right file.
3. No failure, cancellation or interruption leaves a partial or temporary file.
4. Sets in a single scan are independent: one invalid set does not affect another.
5. Exit codes match §13 for every path.
6. A merge of pieces totalling more than 4 GiB succeeds on the 64-bit build *and*
   on the 32-bit Windows build, whose address space is smaller than the data.
7. A set whose members total more than the machine's available disk space is
   refused before writing.
8. Non-ASCII and over-260-character paths work on Windows.
9. The project builds and passes on Windows x86, Windows x64 and Linux x64, with
   no third-party code linked on any of them.
10. On a Linux system with neither `zenity` nor `kdialog` installed, the program
    still runs correctly from a terminal when `--input` is given.

---

## 18. Notes for the implementer

### 18.1 Wording is not specified, and is not a test contract

This document specifies **behaviour**, not phrasing. §6 says a failure message
must *name the offending file*; it does not say what the sentence looks like.
§14 requires a summary line with three counts; it does not prescribe its layout.

Write your own wording. If a test in `tests/` asserts on a specific phrase that
came from the previous implementation, **change the test, not the message** —
that phrase is the previous author's expression, and matching it would make your
implementation a derivative of theirs. Relax such an assertion to what this
document actually requires: the exit code, the side effects on disk, and that a
diagnostic names the file or number concerned.

Two exceptions where text *is* fixed, because they are the interface rather than
prose: the option names in §10, and the output filename shape in §7.

### 18.2 Decisions this document deliberately leaves to you

Each of these is a legitimate choice with no single right answer. Pick one,
write it down in your commit message, and move on:

- how sets are ordered relative to one another during a scan (by title, by first
  piece seen, or by size — all are acceptable)
- the fixed block size used for streaming, and whether it is fixed or adaptive
- the temporary file's name
- the exact progress-bar layout and the wording of every message
- the internal structure: how pieces are represented, how validation is staged
- whether progress is reported per piece or across the whole scan
- the exit path used when several signals arrive at once

### 18.3 Things that are easy to get subtly wrong

- **Piece numbers sort numerically, not as text.** `10` follows `9`. Sorting the
  file names as strings puts `10` before `2` and produces a corrupt result with
  no error.
- **Never reuse a running total as a per-file offset.** If you track bytes across
  the whole merge, that counter is not the count for the piece currently being
  copied. Mixing them silently writes only part of each piece.
- **Sizes are 64-bit on every target**, including x86. A 32-bit build must still
  merge more than 4 GiB (§8, §17.6), so no size may be computed or compared in a
  32-bit type.
- **Rename only after the size check passes** (§7.1, §7.2). Publishing first and
  verifying afterwards leaves a corrupt file on a failure.
- **A validation failure aborts one set, not the scan** (§6, §12). Two good sets
  and one bad one must produce two merged files.
- **Do not delete or truncate an existing output file** without applying §9
  first.

### 18.4 Before you claim it is done

`AGENTS.md` defines an eight-stage check and this project treats it as the
definition of done. In particular:

- the reported check counts are floors, not targets: smoke **86** on Windows and
  **73** on POSIX, robustness **27** on Windows and **26** on POSIX. A drop is a
  coverage regression.
- **Do not cut a release or a tag while any stage is red**, and do not attach
  prebuilt executables — users build their own.
- CI is the only authority on whether the other targets build. Local x64 Windows
  results say nothing about Linux or x86.

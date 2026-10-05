<!-- SPDX-License-Identifier: GPL-3.0-only -->
# AGENTS.md - working rules for this repository

These rules apply to **every** change in this repo, in every session.

The Agent can change any of this when the user asks it to or gives it permission.

---

## 0. The gate

Nothing is "done" until **every pass below has been executed on the final state of
the code** and reported with its evidence. A pass may only be marked green with a
command and its output - never by inspection, reasoning, or "it looks right".

Order matters: passes run in order, and **any fix re-runs every earlier pass**
(see §3). Reporting is mandatory; a silent pass is treated as an unrun pass.

Tooling for the local passes: `tools/check_all.ps1` (Windows) and
`tools/check_all.sh` (POSIX). Run the script rather than the steps by hand so the
result is reproducible.

---

## 0b. Current state: implementation not yet written

This repository contains the specification, the test suites, the bug-check gate
and the build scaffolding — but **no implementation**. It is being written from
scratch against `docs/SPEC.md`, which is behaviour only and contains no code.

The point of writing it fresh is that this code can carry a real licence. An
earlier implementation existed elsewhere and descended from two upstreams that
never declared one, so it was all rights reserved and could not be licensed or
cleanly extended. None of that code is in this repository or its history.

Rules while the implementation is outstanding:

- **Do not go looking for the earlier implementation.** Treat it as unavailable
  rather than convenient. If you find a behaviour worth preserving, record it in
  the spec instead.
- **Do not write the implementation in a context that has read it.** Such a
  session produces a derivative, not independent work, and would undo the reason
  this repository exists. The implementation must be written in a fresh context
  that has only ever seen `docs/SPEC.md`.
- **The existing test suites assert on message wording carried over from that
  earlier implementation.** Do *not* make the new implementation match it. Relax
  those assertions to what `docs/SPEC.md` actually requires: the exit code, the
  side effects on disk, and — for the validation rules in spec section 6 — that
  the output *names the offending file or number*. Wording is deliberately
  unspecified; copying it to satisfy a test is the failure mode to avoid.

## 1. Pass matrix

| # | Pass | Question it answers | How |
| --- | --- | --- | --- |
| 1 | **Build, every shipping config** | Does it compile and link, warning-free? | Build x64 + x86 + a single-config generator, Debug and Release. |
| 2 | **Correctness review** | Is the logic, and the resource/API handling, right? | Read the diff line by line. |
| 3 | **Test-harness integrity** | Would the tests actually catch the bug? | Verify the tests themselves before trusting a green run. |
| 4 | **Behaviour** | Does it work end to end, on every exit path? | `smoke_test.ps1` / `smoke_test.sh`. |
| 5 | **Robustness / hostile input** | Does it survive abuse without corrupting data? | `robustness_test.ps1`, including >2 GiB. |
| 6 | **Packaging** | Is the shipped artefact sound? | `verify_binary.py`. |
| 7 | **Foreign platforms** | Does it build and pass elsewhere? | CI matrix is the only authority. |
| 8 | **Claim accuracy** | Is everything written true? | Audit docs, messages, commit text, release notes. |

### Pass 1 - build, every shipping config

Warning-free at `/W4` (MSVC) and `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
(GCC/Clang). Build **all** of these, not just the default:

```bat
cmake -S . -B build/x64   -G "Visual Studio 17 2022" -A x64   -DPKG_MERGE_WERROR=ON
cmake --build build/x64   --config Release --parallel
cmake -S . -B build/x86   -G "Visual Studio 17 2022" -A Win32 -DPKG_MERGE_WERROR=ON
cmake --build build/x86   --config Release --parallel
REM single-config generator: catches the CMake_BUILD_TYPE default
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build/nmake -G "NMake Makefiles" -DPKG_MERGE_WERROR=ON
cmake --build build/nmake
```

Also build `Debug` at least once: it is the configuration that catches assertions,
`-Wunused` and the debug CRT wiring. Never ship it.

### Pass 2 - correctness review

Read every changed line. Explicitly check:

- index arithmetic, off-by-one, overflow and truncation (`uintmax_t` vs `size_t` vs `int`)
- **cursor vs per-item counters** - a running total must never be reused as a
  per-file offset. This exact bug shipped once and wrote 1 byte per piece.
- iterator/reference lifetime, dangling references after `map`/`vector` growth
- signed/unsigned mixing and narrowing in comparisons and casts
- uninitialised reads; buffer sizes vs the data actually available
- exception paths and early returns: do they leak handles, leave `.tmp` files,
  or leave a half-written destination published?
- large containers passed by value instead of by reference
- every early return that skips cleanup

### Pass 3 - test-harness integrity (do this *before* trusting any green run)

A green suite that cannot fail proves nothing. Before accepting pass 4/5 output:

- Run the suite against a **deliberately broken build** and confirm it goes red.
  Break the smallest possible thing, for example reject every piece with a
  `if (true)`, rebuild, run, confirm failures, then revert.
- Confirm every assertion the suite claims to make actually exists in the file
  (`grep` the assertion text) and that helper functions are all defined. Missing
  helpers have silently aborted two suites mid-run.
- Confirm the test data satisfies its own preconditions (a suite that asserts a
  PKG magic warning needs magic in the fixture).
- Confirm the suite is **invocable**: `bash -n`, executable bit set in the git
  index (`git ls-files -s`), and no use of GNU-only flags in portable scripts
  (`head -c` is unavailable on macOS).
- Confirm the script's exit code is propagated; a suite that prints `FAIL` and
  exits `0` is worse than no suite.

### Pass 4 - behaviour

`tests/smoke_test.ps1` (Windows) / `tests/smoke_test.sh` (POSIX). Every exit code
path must be exercised: success, usage error, validation error, I/O error,
cancellation, overwrite and no-clobber, dry run, recursive, non-ASCII paths.
Expected count today: **73 checks on Windows, 60 on POSIX** (the byte-comparison
helper contributes one check less in the shell version). A drop in either number
is a coverage regression, not a flaky test.

### Pass 5 - robustness / hostile input

`tests/robustness_test.ps1` on Windows, `tests/robustness_test.sh` on POSIX.
Hostile input: empty files, files with no `_N` suffix, duplicate part numbers,
gaps in the sequence, non-numeric parts, absurdly large part numbers, dotted and
spaced titles, mixed-case titles, non-ASCII paths, paths over 260 characters,
200 pieces, shuffled input, read-only output directory, locked source file, and
a merge larger than 2 GiB (32-bit size overflow regression). Expected count
today: **27 checks on Windows, 26 on POSIX** (23 on POSIX with `--skip-large`).

Run the POSIX one too. Permissions, `rename()` over an existing file, `O_EXCL`
and `EINTR` all behave differently from Win32, and the program has a separate
branch for each; a hostile case that only ever runs on Windows is not evidence
about Linux.

Every case must produce a clear diagnostic, a non-zero exit code where it should
fail, and **no partial or temporary file left behind**.

### Pass 6 - packaging

```bat
python tools\verify_binary.py build\x64\Release\pkg_merge.exe 1.0.0
python tools\verify_binary.py build\x86\Release\pkg_merge.exe 1.0.0
```

Asserts: no debug-CRT imports, `DYNAMICBASE | NXCOMPAT | GUARD_CF`, plus
`HIGH_ENTROPY_VA` on x64, version resource present, and that CFG is *enabled*
rather than merely instrumented. When the verifier is changed, validate the
verifier itself (§3 logic applies to tools too).

### Pass 7 - foreign platforms (CI is the only authority)

Local x64 Windows passes say **nothing** about the other targets. Push and read
the run:

```bash
python tools/check_all.py --watch <sha>   # waits, prints every job + failing step
```

All jobs green: Windows x64, Windows x86, Linux. Pass 7 failures in the past
that no local pass could see: `std::wstring_view` narrowing in a POSIX branch,
`-Wunused-const-variable` under Clang, and the VS generator not finding an
instance on the runner image. (macOS is no longer a target, so Apple's `ld`
rejecting GNU `-z relro` cannot recur - but keep the linker flags
platform-conditional anyway.) Never mark this pass done from a local build.

### Pass 8 - claim accuracy

Audit every factual claim in README, `--help`, release notes and the commit
message against reality:

- Does the linked release page belong to *this* repository?
- Does "updated dependency X" mean it was actually updated?
- Are the stated test counts the real counts?
- Are files described as existing actually present?
- Is anything described as tested merely reasoned about?
- Version numbers must match `project(VERSION)` and the tag.

Correct any inaccuracy in the same commit that introduced it.

---

## 2. Severity and what "done" means

| Severity | Example | Requirement |
| --- | --- | --- |
| **S1 data loss / corruption** | Truncated or wrong output published; silent data loss | All passes + an explicit regression test that fails before the fix |
| **S2 crash / hang** | Undefined behaviour, unhandled exception | All passes + a test that reproduces the crash |
| **S3 wrong behaviour** | Wrong exit code, misleading message | All passes |
| **S4 build/packaging** | Won't build, debug CRT, missing mitigation | Passes 1, 6, 7 |
| **S5 cosmetic / docs** | Wording, typos, inaccurate claims | Pass 8 |

---

## 3. Re-run discipline

Any bug found in pass *N* invalidates passes 1..*N-1*. Fix it, then re-run them
all before reporting. When a fix lands, say explicitly which passes were re-run.

---

## 4. House rules

- **GPL-3.0 section 4 matters here.** You may only apply the GPL to code you hold
  rights in. No earlier implementation of this tool is in this repository or its
  history, and none may be copied in or relicensed under the GPL.
- Never ship a `Debug` build or an exe with debug-CRT dependencies.
- Prefer `std::error_code` overloads of `<filesystem>` over throwing calls;
  report errors, never `std::terminate`.
- Every path printed to the console goes through `pkgmerge::path_to_utf8()` (via
  `display_path()`) so non-ASCII paths survive.
- New behaviour needs a matching `--help` entry and a README section.
- Do not `git commit` or `git push` unless explicitly asked.
- Do not cut a release or a tag while any pass is red.
- **Never attach prebuilt executables to a release.** Users build their own copy;
  this also sidesteps shipping unsigned binaries and redistributing code that has
  no license.
- **Ship and test both Windows x64 and x86.** The 32-bit build is deliberately
  kept: this tool targets the PS4 homebrew community, where old 32-bit Windows
  machines are still in use. Its measured cost is 1.6 of the 3.7 CI-minutes per
  run, and the x86 job runs the full suite including the >2 GiB merge, so it is
  not untested surface. Its only real weakness is 8-bit ASLR entropy against 17
  on x64, which is a poor trade for locking users out. Do not drop it unless the
  user asks.
- **Ship and test Windows x64, Windows x86 and Linux x64.** macOS is **not** a
  target: the user dropped it when the rewrite was scoped, for a smaller surface
  and no AppKit path to maintain. Do not add it back without the user asking.
- **Link no third-party code.** Windows uses `IFileDialog`; Linux locates
  `zenity` or `kdialog` at run time. `libs/nativefiledialog-extended` was removed
  and its CMake options deleted. Do not reintroduce a bundled dependency, and do
  not add a Linux build-time package requirement.
- **`pkgmerge/util.h` no longer exists.** Its UTF-8 path helpers went with the
  old implementation; the rewrite must provide its own, and console output must
  still handle non-ASCII paths correctly (spec section 14).

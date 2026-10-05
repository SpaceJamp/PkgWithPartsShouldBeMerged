<!-- SPDX-License-Identifier: GPL-3.0-only -->

# PkgWithPartsShouldBeMerged

Merge the numbered pieces of a split PS4 `.pkg` back into a single PKG.

[![build](https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged/actions/workflows/build.yml/badge.svg)](https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged/actions/workflows/build.yml)

A game too large to move around in one piece arrives split into numbered files.
This tool finds those sets, checks them, and stitches each one back together:

```
CUSA12345_0.pkg   CUSA12345_1.pkg   CUSA12345_2.pkg   ->   CUSA12345-merged.pkg
        piece 0 is the root piece and holds the PKG header
```

---

> ### Status: working
>
> The implementation is in `src/`. It was written against
> [`docs/SPEC.md`](./docs/SPEC.md), a behavioural specification that contains no
> code at all, and it builds warning-free on Windows x86, Windows x64 and Linux
> x64 — CI is green on all three.
>
> **No prebuilt binaries are published.** You build your own copy; see
> [Building from source](#building-from-source). Two things are not yet covered
> by any test: cancelling with the Escape key, and the folder dialogs, both of
> which need a real console session or a desktop.

---

## What it does

| | |
| --- | --- |
| **Targets** | Windows x86-64, Windows x86, Linux x86-64. **macOS is not supported.** |
| **Dependencies** | None. No third-party code is linked, and Linux needs no build packages. |
| **Folder dialog** | Windows: the built-in `IFileDialog`. Linux: `zenity` or `kdialog` if installed. |
| **Prebuilt binaries** | None published. You build your own copy. |
| **Runs on** | Any console. No install step, no redistributable, no runtime. |

---

## Usage

1. Put every piece of a game into one folder. Pieces from different games can
   share that folder.
2. Do any of these:
   - **double-click** the executable and pick the folder in the dialog;
   - **drag** the folder onto the executable (Windows);
   - run it from a terminal: `pkg_merge.exe "D:\PS4 pieces"`.
3. Wait for the summary line and then `Done.`

On Linux with neither `zenity` nor `kdialog` installed, pass `--input` — the
program works fine from a terminal and will say the dialog is unavailable rather
than hang.

### Command line

```
PkgWithPartsShouldBeMerged [OPTIONS] [INPUT_DIR [OUTPUT_DIR]]
```

| Option | Description |
| --- | --- |
| `-i`, `--input DIR` | Folder holding the PKG pieces. A folder picker opens when omitted. |
| `-o`, `--output DIR` | Where to write `<TITLE_ID>-merged.pkg`. Defaults to the input folder. |
| `-r`, `--recursive` | Also scan sub folders. |
| `-f`, `--overwrite` | Replace an existing merged file without asking. |
| `--no-clobber` | Never replace; skip existing merged files. |
| `--backup` | Move the previous merged file aside instead of destroying it. |
| `--verify` | Re-read the finished file and compare it byte for byte against the sources. **On by default.** |
| `--no-verify` | Skip that extra read pass. |
| `-n`, `--dry-run` | Report what would be merged, write nothing. |
| `-q`, `--quiet` | Only print warnings and errors. |
| `--json` | Print the result as a single JSON object on standard output, and nothing else there. |
| `--no-pause` | Do not wait for a key press when the program was double-clicked. |
| `-h`, `--help` | Show the built-in help. |
| `-v`, `--version` | Show the version and the commit it was built from. |

Both the flags and plain positional arguments work, so drag & drop keeps
working:

```bat
pkg_merge.exe "D:\PS4 pieces" "D:\merged"
pkg_merge.exe -i "D:\PS4 pieces" -o "D:\merged"
pkg_merge.exe -i "D:\PS4 pieces" --dry-run --recursive --verify
```

Press `ESC` or `Ctrl+C` to cancel a merge in progress.

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Everything merged. |
| `1` | Nothing could be merged — bad arguments, missing folder, or every title failed validation. |
| `2` | Some titles merged, at least one failed. |
| `130` | Cancelled on request. |

---

## What it checks before merging

A set that is missing pieces, or whose pieces are unreadable, could otherwise be
stitched into a corrupt file while reporting success. Each set is validated
first:

| Check | On failure |
| --- | --- |
| `<TITLE_ID>_0.pkg` (the root piece) is present | error naming the missing file |
| Parts form an unbroken `1..N` sequence | error naming the first gap |
| No part number appears twice (`_1.pkg` vs `_01.pkg`) | error naming both files |
| Every piece is readable and non-empty | that title is skipped entirely |
| Enough free space on the target volume | error before anything is written |
| The written length equals the sum of the pieces | the result is discarded |
| The root piece starts with the PKG magic `7F 43 4E 54` | warning only, the merge still runs |

Titles are matched case-insensitively over ASCII, so `CUSA12345_0.pkg` and
`cusa12345_1.pkg` are treated as one game, and the merged file is named with the
identifier in upper case. Files that are not `.pkg`, and any `*-merged.pkg` this
tool produced earlier, are ignored.

### Safety properties

- Source pieces are opened read-only and are never modified or deleted.
- Output is written to a temporary file and renamed into place only after the
  size check passes, so an interrupted run never publishes a partial file.
- An existing `-merged.pkg` is only replaced after asking (`--overwrite` to skip
  the question, `--no-clobber` to never replace).
- `--backup` moves the previous file aside rather than destroying it, so a
  replacement is recoverable.
- Merges larger than 4 GiB work on the 32-bit build too: nothing is mapped
  whole, and sizes are computed in 64-bit arithmetic.

---

## Building from source

### What you need

| | |
| --- | --- |
| **CMake** | 3.24 or newer — `cmake --version` |
| **Windows** | Visual Studio 2022, or the Build Tools with the C++ workload |
| **Linux** | GCC or Clang with C++20 — nothing else, no `-dev` packages to install |

There is **no third-party code** in this project and Linux needs no GUI or
toolkit development packages, so there is nothing to install before configuring.

### Windows

From an ordinary terminal — CMake finds Visual Studio by itself, so no developer
prompt is needed:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

That produces **`build\Release\pkg_merge.exe`**. Use `-A Win32` instead of
`-A x64` for the 32-bit build.

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That produces **`build/pkg_merge`**. Single-config generators default to Release
on their own, so `-DCMAKE_BUILD_TYPE=Release` is belt and braces.

### Other generators

| Generator | Configure | Executable lands in |
| --- | --- | --- |
| Visual Studio (multi-config) | `-G "Visual Studio 17 2022" -A x64` | `build\Release\pkg_merge.exe` |
| Ninja | `-G Ninja` | `build\pkg_merge.exe` |
| Makefiles / NMake | *(no `-G`)* | `build\pkg_merge.exe` |
| Ninja Multi-Config | `-G "Ninja Multi-Config"` | `build\Release\pkg_merge.exe` |

Two rules cover all of them:

- **Multi-config generators ignore `CMAKE_BUILD_TYPE`.** The build type is chosen
  by `--config Release` at build time, not at configure time.
- **Single-config generators ignore `--config`.** The build type comes from
  `-DCMAKE_BUILD_TYPE=Release`, which this project defaults to Release anyway.

NMake runs `nmake` while configuring, so it only works from a Developer Command
Prompt where the MSVC environment is already on `PATH`.

> **Build Release, not Debug.** A Debug build links against the debug CRT
> (`MSVCP140D.dll`, `VCRUNTIME140D.dll`, `ucrtbased.dll`) and will only start on a
> machine that has Visual Studio installed. CMake warns if you configure a
> non-Release build with a single-config generator.

### Verifying what you built

The repository carries the tools that check a build, and they are the same ones
CI runs:

```bat
python tools\verify_binary.py build\Release\pkg_merge.exe 1.1.0
```

This asserts the binary has no debug-CRT dependency and carries ASLR, DEP and
Control Flow Guard. On x64 it also checks high-entropy ASLR, which does not
exist on 32-bit and is not claimed there.

For the full local gate — builds both architectures, runs every suite — use
`.\tools\check_all.ps1` on Windows or `bash ./tools/check_all.sh` on Linux. See
[Testing](#testing).

### If the build breaks

**Delete `build/` and configure again.** CMake caches the generator, the
compiler and a great deal more, so most confusing failures come from a stale
cache rather than from the source:

```bat
Remove-Item -Recurse -Force build
```

That is also the fix for the bare `generator: build tool execution failed`
message with an empty command after it — a half-written cache left behind by a
failed configure. You cannot switch a `build/` folder to a different generator
by re-running CMake; delete it and start over.

### Build options

| CMake option | Default | Meaning |
| --- | --- | --- |
| `PKG_MERGE_WERROR` | `OFF` | Treat compiler warnings as errors. CI configures with this on. |
| `PKG_MERGE_COPYRIGHT` | `Copyright (c) 2026 SpaceJamp - GPL-3.0-only.` | Text embedded in the Windows version resource. |

### Keeping your copy up to date

You are building from source rather than downloading a binary, so pull before you
rebuild:

```bat
cd C:\path\to\PkgWithPartsShouldBeMerged
git pull
cmake --build build --config Release
```

- `git stash` first if you have local edits and the pull asks you to resolve
  conflicts.
- **`git clone` saying the destination already exists means you already have a
  copy** — use `git pull`, not `git clone`.
- `git log --oneline -1` shows which commit you are on.

---

## Testing

```powershell
.\tools\check_all.ps1                     # Windows: build both architectures + all suites
bash ./tools/check_all.sh                 # Linux
python tools/check_all.py --watch <sha>   # wait for CI (pass 7)
```

CI builds and tests Windows x64, Windows x86 and Linux.

- `tests/smoke_test.ps1` / `tests/smoke_test.sh` - every exit path: 85 checks on
  Windows, 72 on POSIX (the byte-comparison helper reports one check less there).
- `tests/robustness_test.ps1` / `tests/robustness_test.sh` - 27 checks on
  Windows, 26 on POSIX, with hostile input and a merge up to 2.5 GiB.
- `tools/verify_binary.py` — asserts a built binary has no debug-CRT dependencies
  and carries ASLR, DEP and Control Flow Guard.

Contributors: read [`AGENTS.md`](./AGENTS.md). Changes must pass an eight-stage
bug check before they count as done, and the build must be green on all three
targets.

## Project layout

```
docs/SPEC.md             the behavioural specification the implementation is written against
src/                     the implementation
resources/               Windows version resource + application manifest
tests/                   smoke and robustness suites, Windows and POSIX
tools/                   local bug-check gate, binary verifier
```

---

## License

[GPL-3.0-only](LICENSE). The full licence text is in [`LICENSE`](./LICENSE); the
notice at the end of that file records the copyright.

Copyright (c) 2026 SpaceJamp.

You may use, study, modify and redistribute this, including commercially, provided
you pass on the same freedoms and offer the source of anything you distribute.

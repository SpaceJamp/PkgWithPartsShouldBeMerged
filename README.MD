<!-- SPDX-License-Identifier: GPL-3.0-only -->

# PkgWithPartsShouldBeMerged

Merge the numbered pieces of a split PS4 `.pkg` back into a single installable file.

[![build](https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged/actions/workflows/build.yml/badge.svg)](https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged/actions/workflows/build.yml)

A game too large to move around in one piece arrives split into numbered files.
This tool finds those sets, checks them, and stitches each one back together:

```
CUSA12345_0.pkg   CUSA12345_1.pkg   CUSA12345_2.pkg   ->   CUSA12345-merged.pkg
        piece 0 is the root piece and holds the PKG header
```

---

> ### Status: under construction
>
> There is no implementation in this repository yet. It is being written from
> scratch against [`docs/SPEC.md`](./docs/SPEC.md), a behavioural specification
> containing no code at all — see [Why a rewrite](#why-a-rewrite) below.
>
> **`master` does not build yet, and the badge is red for that reason.** What is
> here today is the specification, the test suites that will hold the
> implementation to it, the bug-check gate, and the build and CI scaffolding.
>
> Everything below documents the behaviour the implementation must have.

---

## What it will do

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
   - run it from a terminal: `PkgWithPartsShouldBeMerged.exe "D:\PS4 pieces"`.
3. Wait for `[success] completed`.

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
| `--verify` | Re-read the finished file and compare it byte for byte against the sources. |
| `-n`, `--dry-run` | Report what would be merged, write nothing. |
| `-q`, `--quiet` | Only print warnings and errors. |
| `--no-pause` | Do not wait for a key press when the program was double-clicked. |
| `-h`, `--help` | Show the built-in help. |
| `-v`, `--version` | Show the version. |

Both the flags and plain positional arguments work, so drag & drop keeps
working:

```bat
PkgWithPartsShouldBeMerged.exe "D:\PS4 pieces" "D:\merged"
PkgWithPartsShouldBeMerged.exe -i "D:\PS4 pieces" -o "D:\merged"
PkgWithPartsShouldBeMerged.exe -i "D:\PS4 pieces" --dry-run --recursive --verify
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

The original tool trusted its input, which could silently produce a corrupt PKG
while reporting success. Each set is validated first:

| Check | On failure |
| --- | --- |
| `<TITLE_ID>_0.pkg` (the root piece) is present | error naming the missing file |
| Parts form an unbroken `1..N` sequence | error naming the first gap |
| No part number appears twice (`_1.pkg` vs `_01.pkg`) | error naming both files |
| Every piece is readable and non-empty | that title is skipped entirely |
| Enough free space on the target volume | error before anything is written |
| The written length equals the sum of the pieces | the result is discarded |
| The root piece starts with the PKG magic `7F 43 4E 54` | warning only, the merge still runs |

Titles are matched case-insensitively, so `CUSA12345_0.pkg` and `cusa12345_1.pkg`
are treated as one game. Files that are not `.pkg`, and any `*-merged.pkg` this
tool produced earlier, are ignored.

### Safety properties

- Source pieces are opened read-only and are never modified or deleted.
- Output is written to a temporary file and renamed into place only after the
  size check passes, so an interrupted run never publishes a partial file.
- An existing `-merged.pkg` is only replaced after asking (`--overwrite` to skip
  the question, `--no-clobber` to never replace).
- Merges larger than 4 GiB work on the 32-bit build too: nothing is mapped
  whole, and sizes are computed in 64-bit arithmetic.

---

## Building from source

CMake 3.24 or newer and a C++20 compiler (Visual Studio 2022 on Windows, GCC or
Clang on Linux).

**Windows, Visual Studio installed** — CMake finds Visual Studio by itself, no
developer prompt needed:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Use `-A Win32` for the 32-bit build. Multi-config generators ignore
`CMAKE_BUILD_TYPE`, so `--config Release` is what selects the build type.

**Linux, any Makefile generator** — no extra packages:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**With Ninja**, on either platform — only if you have Ninja installed
(`ninja --version`); otherwise CMake fails with *unable to find a build program
corresponding to "Ninja"*:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Windows, NMake** — only from a developer prompt, because this generator runs
`nmake` while configuring and needs it on `PATH`:

```bat
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G "NMake Makefiles"
cmake --build build
```

Single-config generators default to Release automatically; with the Visual Studio
generator pass `--config Release` explicitly. To retype an existing build folder,
clear the cached value first: `cmake -U CMAKE_BUILD_TYPE -S . -B build -G Ninja`.

> **Generator changes need a clean folder.** CMake caches the generator, so if a
> `build/` directory was created with one generator you cannot switch it by
> re-running with another — delete the `build/` folder and configure again. This
> also clears a half-written cache left behind by a failed configure, which is
> what produces the bare *generator: build tool execution failed* message with an
> empty command after it.

| CMake option | Default | Meaning |
| --- | --- | --- |
| `PkgWithPartsShouldBeMerged_WERROR` | `OFF` | Treat compiler warnings as errors. |
| `PkgWithPartsShouldBeMerged_COPYRIGHT` | credits the original authors | Text embedded in the Windows version resource. |

### Keeping your copy up to date

Pull before you rebuild — you are building from source rather than downloading a
binary:

```bat
cd C:\path\to\PkgWithPartsShouldBeMerged
git pull
```

- **Delete `build/` if the build breaks.** `CMakeLists.txt` changes frequently and
  CMake caches a great deal of configuration: `Remove-Item -Recurse -Force build`
- **If `git clone` says the destination already exists**, you have a copy — `git pull`.
- **Check which commit you are on** with `git log --oneline -1`.

`git stash` first if you have local edits and a pull asks you to resolve conflicts.

---

## Testing

```powershell
.\tools\check_all.ps1                     # Windows: build both architectures + all suites
bash ./tools/check_all.sh                 # Linux
python tools/check_all.py --watch <sha>   # wait for CI (pass 7)
```

CI builds and tests Windows x64, Windows x86 and Linux.

- `tests/smoke_test.ps1` / `tests/smoke_test.sh` — every exit path: 65 checks on
  Windows, 54 on POSIX (the byte-comparison helper reports one check less there).
- `tests/robustness_test.ps1` — 27 checks with hostile input, up to a 2.5 GiB merge.
- `tools/verify_binary.py` — asserts a built binary has no debug-CRT dependencies
  and carries ASLR, DEP and Control Flow Guard.

Contributors: read [`AGENTS.md`](./AGENTS.md). Changes must pass an eight-stage
bug check before they count as done, and the build must be green on all three
targets.

## Project layout

```
docs/SPEC.md             the behavioural specification the implementation is written against
resources/               Windows version resource + application manifest
tests/                   smoke and robustness suites
tools/                   local bug-check gate, binary verifier
```

---

## Why a rewrite

The earlier implementation of this tool - which lived in
[`SpaceJamp/PkgWithPartsShouldBeMerged`](https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged) - derives from two
upstream projects that never declared a license:

- **[Tustin & 0x199](https://github.com/Tustin/PkgWithPartsShouldBeMerged)** — the original
  `PkgWithPartsShouldBeMerged`. The core idea, the `<TITLE_ID>_<N>.pkg` layout and the shape of the
  merging code are theirs.
- **[aldo-o](https://github.com/aldo-o/PkgWithPartsShouldBeMerged)** — the fork that added macOS
  support and the native folder picker (`v3.0b`), and later the `-i` / `-o`
  arguments and an optional output directory.
- **[btzy](https://github.com/btzy/nativefiledialog-extended)** — author of the
  folder dialog those versions used, now neither used nor needed.

Because neither upstream ever granted a license, that code was all rights
reserved and **could not be relicensed** - which made extending it awkward and
publishing it impossible to do cleanly. Re-implementing it from a specification,
rather than asking permission for code that has sat untouched since 2018 and 2025,
produces something that can be licensed and maintained on its own terms.

`docs/SPEC.md` states *what* the program must do and *how it must behave*. It
contains no code, no algorithms and no data structures, and it was written from
the PKG container format and the required behaviour rather than from the previous
implementation. The code is then written from that specification, which is what
makes this repository's GPL-3.0 licence a clean claim rather than a borrowed one.

The predecessor repository keeps the full history - 27 commits, including the
upstream lineage - and is retained as the record of what came before.

---

## Credits and acknowledgements

This tool would not exist without the people who built it first, and none of this
work is theirs to lose:

- **[Tustin & 0x199](https://github.com/Tustin/PkgWithPartsShouldBeMerged)** — the original
  `PkgWithPartsShouldBeMerged`, and the idea this tool implements.
- **[aldo-o](https://github.com/aldo-o/PkgWithPartsShouldBeMerged)** — the fork that brought it to
  macOS and gave it a command line.
- **[btzy](https://github.com/btzy/nativefiledialog-extended)** — the folder
  dialog the earlier versions used.

If you use this, please pass the credit on.

---

## License

[GPL-3.0-only](LICENSE) — see [`LICENSE.md`](./LICENSE.md) for provenance and scope.

Copyright (c) 2026 SpaceJamp.

You may use, study, modify and redistribute this, including commercially, provided
you pass on the same freedoms and offer the source of anything you distribute.

This licence covers everything in this repository. It does **not** retroactively
license the earlier `SpaceJamp/PkgWithPartsShouldBeMerged` code, which remains all rights
reserved.
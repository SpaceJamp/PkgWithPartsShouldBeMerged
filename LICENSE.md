# SPDX-License-Identifier: GPL-3.0-only

# Licensing and provenance

This repository is licensed under the **GNU General Public License, version 3**
(`GPL-3.0-only`). The canonical text is in [`LICENSE`](LICENSE).

Copyright (c) 2026 SpaceJamp.

## What the GPL covers

Everything committed to this repository: the specification in `docs/SPEC.md`, the
test suites, the bug-check tooling, the build and CI configuration, the Windows
resources, and the implementation as it lands.

Anyone may use, study, modify and redistribute it — including commercially —
provided any distributed work carries this same licence and offers its source.

## What it does not cover

**Any earlier version of the code.** This licence applies from the date it was
added. It cannot and does not retroactively license the implementation that lived
in [`SpaceJamp/pkg-merge`](https://github.com/SpaceJamp/pkg-merge), which remains
**all rights reserved**.

That matters if you obtained a copy of that code before this repository existed:
you hold no rights to it. If you need to use, redistribute or build on it, ask the
original authors.

GPL-3.0 section 4 is the operative constraint here: you may only apply these
terms to a work if you hold copyright or licence rights in it. This repository
is a re-implementation written from a specification, and contains no upstream
code — which is precisely why it can be licensed at all.

## Provenance of the earlier code

The previous implementation was never licensed, because it derived from two
upstream projects that never declared one:

| Source | License |
| --- | --- |
| [Tustin/pkg-merge](https://github.com/Tustin/pkg-merge) by Tustin & 0x199 — the original | none declared |
| [aldo-o/pkg-merge](https://github.com/aldo-o/pkg-merge) — the fork this line came through | none declared |
| `nativefiledialog-extended` by btzy — used by those versions, no longer used | permissive |

GitHub's licence API returns 404 for both upstreams, so the default "all rights
reserved" applied. Copyright does not transfer by way of a fork: GitHub's terms
permit viewing and forking a public repository, which is a server-side permission,
not a grant of rights in the code.

With no licence available from either author — one has been inactive since 2018,
and neither could grant rights over the other's contributions anyway — the code
was re-implemented from a written specification instead. `docs/SPEC.md` describes
the required behaviour and contains no code; the implementation is written from
that specification by a process that has not read the earlier implementation.

The predecessor repository is retained, with its full history, as the record of
what came before. It is **not** covered by this licence and must not be relicensed
under it.

## Credits

Attribution is separate from licensing, and is owed regardless. The GPL requires
the licence notice to survive; these names explain what the tool *is*:

- **Tustin & 0x199** — the original `pkg-merge`, and the idea this implements.
- **aldo-o** — the fork that brought it to macOS and gave it a command line.
- **btzy** — author of `nativefiledialog-extended`.

See the credits section of `README.MD`. If you use this, please pass the credit
on.

## For maintainers

If you fork or extend this, keep the credits and this provenance note. The GPL
obliges you to pass on the same freedoms; this file is the part that keeps the
honest account of where the work came from, and that obligation is independent of
the licence.
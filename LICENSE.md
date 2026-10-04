<!-- SPDX-License-Identifier: MIT -->

# Licensing and provenance

This repository is licensed under the [MIT License](LICENSE),
Copyright (c) 2026 SpaceJamp.

## What the MIT licence covers

Everything committed to this repository: the specification in `docs/SPEC.md`, the
test suites, the bug-check tooling, the build and CI configuration, the Windows
resources, and the implementation as it lands.

## What it does not cover

**Any earlier version of the code.** The MIT licence applies from the date it was
added. It cannot and does not retroactively license the implementation that lived
in [`SpaceJamp/pkg-merge`](https://github.com/SpaceJamp/pkg-merge), which remains
**all rights reserved**.

That matters if you obtained a copy of that code before this repository existed:
you hold no rights to it from this licence. If you need to use, redistribute or
build on it, ask the original authors.

## Provenance of the earlier code

The previous implementation was never licensed, because it derived from two
upstream projects that never declared one:

| Source | License |
| --- | --- |
| [Tustin/pkg-merge](https://github.com/Tustin/pkg-merge) by Tustin & 0x199 - the original | none declared |
| [aldo-o/pkg-merge](https://github.com/aldo-o/pkg-merge) - the fork this line came through | none declared |
| `nativefiledialog-extended` by btzy - used by those versions, no longer used | permissive |

GitHub's licence API returns 404 for both upstreams, so the default "all rights
reserved" applied. Copyright does not transfer by way of a fork: GitHub's terms
permit viewing and forking a public repository, which is a server-side permission,
not a grant of rights in the code.

With no licence available from either author - one has been inactive since 2018,
and neither could grant rights over the other's contributions anyway - the code
was re-implemented from a written specification instead. `docs/SPEC.md` describes
the required behaviour and contains no code; the implementation is written from
that specification by a process that has not read the earlier implementation.
That is what makes the MIT claim here clean rather than borrowed.

The predecessor repository is retained, with its full history, as the record of
what came before.

## Credits

Attribution is separate from licensing, and is owed regardless:

- **Tustin & 0x199** - the original `pkg-merge`, and the idea this implements.
- **aldo-o** - the fork that added macOS support and the command line.
- **btzy** - author of `nativefiledialog-extended`.

See the credits section of `README.MD`. If you use this tool, please pass the
credit on.

## For maintainers

If you fork or extend this, keep the credits and this provenance note. The
licence is only as good as the honesty of the account of where the work came
from.
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the packaging properties of a built pkg_merge binary.

Checks the things that silently regress: a debug-CRT dependency, a missing
mitigation flag, a stripped/absent version resource.

Usage: python tools/verify_binary.py <path-to-exe> [expected-version]
"""
from __future__ import annotations

import struct
import sys

FLAG_NAMES = [
    (0x0020, "HIGH_ENTROPY_VA"),
    (0x0040, "DYNAMIC_BASE"),
    (0x0080, "FORCE_INTEGRITY"),
    (0x0100, "NX_COMPAT"),
    (0x0200, "NO_ISOLATION"),
    (0x0400, "NO_SEH"),
    (0x0800, "NO_BIND"),
    (0x1000, "APPCONTAINER"),
    (0x4000, "GUARD_CF"),
    (0x8000, "TERMINAL_SERVER_AWARE"),
]

DEBUG_CRT = ("MSVCP140D.dll", "VCRUNTIME140D.dll", "ucrtbased.dll", "MSVCP140D", "VCRUNTIME140D")


class Pe:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pe = struct.unpack_from("<I", data, 0x3C)[0]
        self.machine, self.sections = struct.unpack_from("<HH", data, self.pe + 4)
        self.opt = self.pe + 24
        self.opt_size = struct.unpack_from("<H", data, self.pe + 20)[0]
        self.is_64 = struct.unpack_from("<H", data, self.opt)[0] == 0x20B
        self.entry = struct.unpack_from("<I", data, self.opt + 16)[0]
        self.subsystem = struct.unpack_from("<H", data, self.opt + 68)[0]
        self.dll_chars = struct.unpack_from("<H", data, self.opt + 70)[0]
        self.image_base = (
            struct.unpack_from("<Q", data, self.opt + 24)[0]
            if self.is_64
            else struct.unpack_from("<I", data, self.opt + 28)[0]
        )
        dirs_at = self.opt + (112 if self.is_64 else 96)
        self.import_rva = struct.unpack_from("<I", data, dirs_at + 8)[0]
        self.security_rva = struct.unpack_from("<I", data, dirs_at + 4 * 8)[0]
        self.load_config_rva = struct.unpack_from("<I", data, dirs_at + 10 * 8)[0]
        self.sections_table = []
        base = self.pe + 24 + self.opt_size
        for i in range(self.sections):
            off = base + i * 40
            name = data[off : off + 8].rstrip(b"\x00").decode("latin1")
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
            self.sections_table.append((name, vaddr, vsize, raddr, rsize))

    def rva_to_offset(self, rva: int) -> int | None:
        for _name, vaddr, vsize, raddr, rsize in self.sections_table:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raddr + (rva - vaddr)
        return None

    def imported_dlls(self) -> list[str]:
        offset = self.rva_to_offset(self.import_rva)
        if offset is None:
            return []
        names = []
        step = 20
        index = 0
        while True:
            entry = offset + index * step
            name_rva = struct.unpack_from("<I", self.data, entry + 12)[0]
            if name_rva == 0:
                break
            name_off = self.rva_to_offset(name_rva)
            end = self.data.index(b"\x00", name_off)
            names.append(self.data[name_off:end].decode("latin1"))
            index += 1
        return names

    def import_names(self) -> list[str]:
        offset = self.rva_to_offset(self.import_rva)
        found: list[str] = []
        index = 0
        step = 20
        width = 8 if self.is_64 else 4
        ordinal_flag = 1 << 63 if self.is_64 else 1 << 31
        while True:
            entry = offset + index * step
            lookup_rva = struct.unpack_from("<I", self.data, entry)[0]
            name_rva = struct.unpack_from("<I", self.data, entry + 12)[0]
            if name_rva == 0:
                break
            if lookup_rva:
                thunk = self.rva_to_offset(lookup_rva)
                k = 0
                while True:
                    raw = struct.unpack_from("<Q" if self.is_64 else "<I", self.data, thunk + k * width)[0]
                    if raw == 0:
                        break
                    if not raw & ordinal_flag:
                        hint = self.rva_to_offset(raw & 0x7FFFFFFF)
                        stop = self.data.index(b"\x00", hint + 2)
                        found.append(self.data[hint + 2 : stop].decode("latin1"))
                    k += 1
            index += 1
        return found

    def guard_flags(self) -> int | None:
        if not self.load_config_rva:
            return None
        offset = self.rva_to_offset(self.load_config_rva)
        if offset is None:
            return None
        if self.is_64:
            return struct.unpack_from("<I", self.data, offset + 0x90)[0]
        return struct.unpack_from("<I", self.data, offset + 0x58)[0]


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    expected_version = sys.argv[2] if len(sys.argv) > 2 else None
    with open(path, "rb") as handle:
        pe = Pe(handle.read())

    failures: list[str] = []
    notes: list[str] = []

    arch = {0x014C: "x86", 0x8664: "x64"}.get(pe.machine, hex(pe.machine))
    print(f"file        : {path}")
    print(f"arch        : {arch}")
    print(f"entry point : 0x{pe.entry:X}")
    print(f"image base  : 0x{pe.image_base:X}")
    print(f"subsystem   : {pe.subsystem} (2=GUI, 3=console)")

    present = [name for bit, name in FLAG_NAMES if pe.dll_chars & bit]
    missing = [name for bit, name in FLAG_NAMES if not pe.dll_chars & bit]
    print(f"dll chars   : 0x{pe.dll_chars:04X} -> {', '.join(present)}")
    print(f"missing     : {', '.join(missing)}")

    dlls = pe.imported_dlls()
    print(f"imports     : {', '.join(dlls)}")

    symbols = pe.import_names()
    crt_debug = sorted({s for s in symbols if any(bad in s for bad in DEBUG_CRT)})
    if crt_debug:
        failures.append(f"debug CRT symbols imported: {crt_debug}")
    dbg_reports = [s for s in symbols if "_CrtDbgReport" in s or "__CrtDbgReport" in s]
    if dbg_reports:
        failures.append(f"debug heap reporting imported: {dbg_reports}")

    for required in ("DYNAMIC_BASE", "NX_COMPAT", "GUARD_CF"):
        if required not in present:
            failures.append(f"required mitigation missing: {required}")
    if arch == "x64" and "HIGH_ENTROPY_VA" not in present:
        failures.append("x64 build without HIGH_ENTROPY_VA (ASLR entropy limited to 8 bits)")

    guard = pe.guard_flags()
    if guard is not None:
        instrumented = bool(guard & 0x100)
        print(f"guard flags : 0x{guard:X} (instrumented={instrumented})")
        if instrumented and "GUARD_CF" not in present:
            failures.append("CFG instrumented but the image is not marked as CFG-enabled")

    if pe.security_rva:
        print("signed      : yes (certificate table present)")
    else:
        print("signed      : no (expected until a code-signing certificate is configured)")

    if expected_version:
        if expected_version.encode("utf-16-le") not in pe.data:
            failures.append(f"version resource does not contain {expected_version!r}")
        else:
            notes.append(f"version resource contains {expected_version}")
    else:
        notes.append("version resource not checked (no expected version given)")

    print()
    for note in notes:
        print(f"note    : {note}")
    for failure in failures:
        print(f"FAILURE : {failure}")
    if failures:
        return 1
    print("OK      : packaging checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
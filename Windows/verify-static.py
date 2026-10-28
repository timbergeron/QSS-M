#!/usr/bin/env python3
"""Reject third-party DLL imports (including delay imports) and packaged DLLs."""

import argparse
from pathlib import Path
import struct
import zipfile

SYSTEM_DLLS = set("""
advapi32.dll bcrypt.dll cfgmgr32.dll comctl32.dll comdlg32.dll crypt32.dll
d3d9.dll d3d11.dll d3d12.dll dinput8.dll dsound.dll dwmapi.dll dxgi.dll
gdi32.dll hid.dll imm32.dll iphlpapi.dll kernel32.dll kernelbase.dll
msvcrt.dll ncrypt.dll ntdll.dll normaliz.dll ole32.dll oleaut32.dll opengl32.dll
powrprof.dll propsys.dll psapi.dll rpcrt4.dll secur32.dll setupapi.dll
shell32.dll shlwapi.dll user32.dll userenv.dll usp10.dll uxtheme.dll
version.dll winhttp.dll wininet.dll winmm.dll winspool.drv ws2_32.dll
wtsapi32.dll xinput1_4.dll
""".split())


def imports(data):
    def u16(offset):
        return struct.unpack_from("<H", data, offset)[0]

    def u32(offset):
        return struct.unpack_from("<I", data, offset)[0]

    if data[:2] != b"MZ":
        raise ValueError("Not a Windows executable")
    pe = u32(0x3c)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    optional = pe + 24
    magic = u16(optional)
    if magic not in (0x10b, 0x20b):
        raise ValueError("Unsupported PE optional header")
    directories = optional + (96 if magic == 0x10b else 112)
    count = u32(directories - 4)
    sections = pe + 24 + u16(pe + 20)

    def offset(rva):
        if rva < u32(optional + 60):
            return rva
        for i in range(u16(pe + 6)):
            section = sections + i * 40
            start = u32(section + 12)
            if start <= rva < start + max(u32(section + 8), u32(section + 16)):
                return u32(section + 20) + rva - start
        raise ValueError(f"Unmapped PE RVA: {rva:x}")

    result = set()
    for index, size, name_field in ((1, 20, 12), (13, 32, 4)):
        if index >= count:
            continue
        rva = u32(directories + 8 * index)
        length = u32(directories + 8 * index + 4)
        if not rva:
            continue
        table = offset(rva)
        for entry in range(table, table + length, size):
            if not any(data[entry:entry + size]):
                break
            name = u32(entry + name_field)
            if index == 13 and not u32(entry) & 1:
                image_base = (u32(optional + 28) if magic == 0x10b else
                              struct.unpack_from("<Q", data, optional + 24)[0])
                name -= image_base
            pos = offset(name)
            result.add(data[pos:data.index(b"\0", pos)].decode("ascii").lower())
    return result


def verify(executable, archive=None):
    dependencies = imports(executable.read_bytes())
    unexpected = sorted(name for name in dependencies if name not in SYSTEM_DLLS
                        and not (name.startswith(("api-ms-win-", "ext-ms-win-"))
                                 and not name.startswith("api-ms-win-crt-")))
    if unexpected:
        raise ValueError("Non-system DLL imports: " + ", ".join(unexpected))
    if archive:
        with zipfile.ZipFile(archive) as package:
            names = package.namelist()
            if any(name.lower().endswith(".dll") for name in names):
                raise ValueError("The static Windows archive contains DLLs")
            for required in (executable.name, "qssm.pak", "quakespasm.pak", "gamecontrollerdb.txt",
                             "third-party-licenses.txt", "static-sources.json"):
                if required not in names:
                    raise ValueError("Archive missing: " + required)
            if package.read(executable.name) != executable.read_bytes():
                raise ValueError("Archive contains a different executable")
    print(f"{executable.name}: system DLLs only: {', '.join(sorted(dependencies))}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    try:
        verify(args.executable, args.archive)
    except (ValueError, struct.error, IndexError) as error:
        parser.exit(1, f"Static Windows verification failed: {error}\n")

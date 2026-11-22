#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Write an IPSwitch (pchtxt) mod that makes a Switch 1 game answer "yes" when its SDK asks
whether it runs on a Switch 2.

The fact lives in the game's own SDK image: `nn::pl::IsRunningOnOunce()` is compiled to a
constant-false stub (`mov w0, wzr; ret`) in Switch 1 SDK builds, and Nintendo's compatibility
layer replaces that symbol on the console. This tool finds the exported symbol in one NSO, checks
that it really is that stub, and writes the one-word patch that turns the answer true, so the
game's Switch 2 code paths become reachable without rebuilding the emulator.

Usage:
    sw2_identity_pchtxt.py <module.nso> [--out DIR] [--name "mod name"] [--symbol SYMBOL]

Where the NSO comes from:
    * Eglantine's own dump: enable "Dump NSO" (Debug tab), boot the game once, and take
      `<dump>/nso/sdk-<build id>.nso` - the file the emulator actually loaded. The build id in
      its name is the one the mod has to match.
    * Or any NSO extracted from ExeFS.

Install:
    <Eglantine data dir>/load/<title id>/<mod name>/exefs/<anything>.pchtxt
    (same folder the game's "Add-Ons" tab lists as a mod), then start the game.

The same switch exists as an option in the emulator since "Switch 2 identity" (game properties ->
System); this mod is for testing on a build that predates it, and it patches one module for one
title, so it cannot leak into other games.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

NSO_HEADER_SIZE = 0x100
BUILD_ID_OFFSET = 0x40
BUILD_ID_SIZE = 0x20
MOD0_MAGIC = 0x30444F4D  # "MOD0"

ELF_DT_NULL = 0
ELF_DT_STRTAB = 5
ELF_DT_SYMTAB = 6
ELF_DT_SYMENT = 11
ELF64_SYM_SIZE = 24
ELF64_SYM_VALUE_OFFSET = 8
ELF64_SYM_SIZE_OFFSET = 16
ELF64_DYN_SIZE = 16

DEFAULT_SYMBOL = "_ZN2nn2pl16IsRunningOnOunceEv"

STUB_FALSE_MOVE = 0x2A1F03E0  # mov w0, wzr
STUB_RETURN = 0xD65F03C0      # ret
STUB_TRUE_MOVE = 0x52800020   # mov w0, #1


class NsoError(RuntimeError):
    pass


def read_at(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or offset + size > len(data):
        raise NsoError(f"read of {size} bytes at {offset:#x} is outside the image")
    return data[offset:offset + size]


def load_nso(path: Path) -> tuple[bytes, bytes]:
    """Returns (image, build_id). The image is the decompressed content the patch addresses."""
    raw = path.read_bytes()
    if len(raw) < NSO_HEADER_SIZE:
        raise NsoError(f"{path} is too small to be an NSO")
    magic = raw[0:4]
    if magic != b"NSO0":
        raise NsoError(
            f"{path} does not start with NSO0 (got {magic!r}). Extract the module first; "
            "an `exefs/` folder holds NSO files directly."
        )
    build_id = raw[BUILD_ID_OFFSET:BUILD_ID_OFFSET + BUILD_ID_SIZE]
    from_name = re.search(r"[0-9a-fA-F]{64}", path.stem)
    if from_name is not None:
        name_build_id = bytes.fromhex(from_name.group(0))
        if name_build_id != build_id and any(build_id):
            print(
                f"note: the build id in the file name ({name_build_id.hex()}) differs from the "
                f"header ({build_id.hex()}); using the header",
                file=sys.stderr,
            )
    return raw[NSO_HEADER_SIZE:], build_id


def probe_dynsym(image: bytes) -> tuple[int, int, int]:
    """Mirrors Core::NvnHle::ProbeDynsym: returns (symtab, strtab, entry size)."""
    if len(image) < 8:
        raise NsoError("the image is too small to hold a MOD0 header")
    (module_offset,) = struct.unpack_from("<I", image, 4)
    if module_offset + 8 > len(image):
        raise NsoError(f"MOD0 offset {module_offset:#x} is outside the image")
    magic, dynamic_offset = struct.unpack_from("<II", image, module_offset)
    if magic != MOD0_MAGIC:
        raise NsoError(
            "no MOD0 header: this module has no dynamic symbol table, so the SDK answer cannot be "
            "patched by symbol"
        )

    symtab = strtab = 0
    entry_size = ELF64_SYM_SIZE
    cursor = module_offset + dynamic_offset
    for _ in range(4096):
        tag, value = struct.unpack_from("<QQ", read_at(image, cursor, ELF64_DYN_SIZE), 0)
        if tag == ELF_DT_NULL:
            break
        if tag == ELF_DT_SYMTAB:
            symtab = value
        elif tag == ELF_DT_STRTAB:
            strtab = value
        elif tag == ELF_DT_SYMENT:
            entry_size = value
        cursor += ELF64_DYN_SIZE
    else:
        raise NsoError("the dynamic section has no terminator; the image looks corrupt")

    if symtab == 0 or strtab <= symtab or strtab > len(image):
        raise NsoError("the dynamic section has no usable symbol/string table")
    return symtab, strtab, max(entry_size, ELF64_SYM_SIZE)


def find_symbol(image: bytes, symbol: str) -> tuple[int, int]:
    """Returns (st_value, st_size) of a defined symbol, matching Core::NvnHle::FindDynsymHits."""
    symtab, strtab, entry_size = probe_dynsym(image)
    found: dict[str, tuple[int, int]] = {}
    entry = symtab
    while entry + ELF64_SYM_SIZE <= strtab:
        st_name, = struct.unpack_from("<I", image, entry)
        st_value, st_size = struct.unpack_from("<QQ", image, entry + ELF64_SYM_VALUE_OFFSET)
        name_offset = strtab + st_name
        if name_offset < len(image):
            name = image[name_offset:image.find(b"\0", name_offset)]
            if name:
                found[name.decode("latin-1")] = (st_value, st_size)
        entry += entry_size

    if symbol not in found:
        close = [name for name in found if "IsRunningOnOunce" in name]
        hint = f" Candidates in this module: {close}" if close else ""
        raise NsoError(f"{symbol} is not exported by this module.{hint}")
    return found[symbol]


def build_patch(image: bytes, symbol: str) -> tuple[int, int]:
    """Checks the stub and returns (offset, patch word) for the pchtxt line."""
    value, size = find_symbol(image, symbol)
    if value == 0:
        raise NsoError(f"{symbol} is undefined in this module")
    if value + 8 > len(image):
        raise NsoError(f"{symbol} at {value:#x} lies past the image")
    word, next_word = struct.unpack_from("<II", image, value)
    if word == STUB_TRUE_MOVE:
        raise NsoError(
            f"{symbol} at {value:#x} already answers true (patched already, or a Switch 2 SDK)"
        )
    if word != STUB_FALSE_MOVE or next_word != STUB_RETURN:
        raise NsoError(
            f"{symbol} at {value:#x} holds {word:#010x} {next_word:#010x}, not the "
            f"constant-false stub ({STUB_FALSE_MOVE:#010x} {STUB_RETURN:#010x}); this build "
            f"decides another way, so a one-word patch is not the fix"
        )
    print(f"found {symbol} at {value:#x} (symbol size {size}), stub verified")
    return value, STUB_TRUE_MOVE


def render_pchtxt(build_id: bytes, offset: int, word: int, module: str) -> str:
    # IPSwitch offsets address the decompressed module image, and hex words are written
    # big-endian in the text and byte-swapped by the parser, so `52800020` lands as
    # `20 00 80 52` in memory - the little-endian encoding of `mov w0, #1`.
    return (
        f"@nsobid-{build_id.hex()}\n"
        f"@enabled\n"
        f"# Switch 2 identity for {module}: {DEFAULT_SYMBOL} answers true instead of false.\n"
        f"# Generated by tools/nx2/sw2_identity_pchtxt.py; remove the file to undo.\n"
        f"{offset:X} {word:08X}\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Write an IPSwitch mod that answers 'yes' to nn::pl::IsRunningOnOunce().",
    )
    parser.add_argument("nso", type=Path, help="the module's NSO (Eglantine's nso dump or ExeFS)")
    parser.add_argument("--out", type=Path, default=Path.cwd(),
                        help="directory for the .pchtxt (default: current directory)")
    parser.add_argument("--file-name", default="switch2-identity.pchtxt",
                        help="file name to write (default: switch2-identity.pchtxt)")
    parser.add_argument("--symbol", default=DEFAULT_SYMBOL,
                        help=f"mangled symbol to patch (default: {DEFAULT_SYMBOL})")
    parser.add_argument("--print-only", action="store_true",
                        help="print the patch instead of writing a file")
    args = parser.parse_args()

    try:
        image, build_id = load_nso(args.nso)
        if not any(build_id):
            raise NsoError("the NSO header carries an empty build id; the mod could never match")
        offset, word = build_patch(image, args.symbol)
    except NsoError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    text = render_pchtxt(build_id, offset, word, args.nso.stem)
    if args.print_only:
        print(text, end="")
        return 0

    args.out.mkdir(parents=True, exist_ok=True)
    destination = args.out / args.file_name
    destination.write_text(text, encoding="utf-8")
    print(f"wrote {destination}")
    print("install it as:")
    print(f"  <Eglantine data dir>/load/<title id>/<mod name>/exefs/{args.file_name}")
    print(f"  (build id {build_id.hex()} - it only applies to the module that carries it)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

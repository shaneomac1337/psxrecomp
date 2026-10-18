#!/usr/bin/env python3
"""[recompiler] symbols: symbols.toml names label generated bodies and feed the
runtime name table (docs/SYMBOLS.md).

  - a named body gets a `/* Name */` line above its func_ definition; unnamed
    bodies do not, and C symbols stay func_XXXXXXXX;
  - the dispatch file registers a {lo, hi, name} table whose rows span each
    named entry up to the next compiled entry; with --compiler it is linked
    against runtime/src/psx_game_symbols.c and looked up from any segment;
  - [[func]] entries with an `overlay` key are not EXE names;
  - two names for one pc fail the build.

Usage: python test_symbol_names_codegen.py [--recompiler <psxrecomp-game>]
                                           [--compiler <cc>]
Exit 0 = PASS.
"""
import argparse
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

LOAD = 0x80010000
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x03FFFFFF)


def make_psxexe():
    size = 0x1000
    header = bytearray(2048)
    header[0:8] = b"PS-X EXE"
    struct.pack_into("<I", header, 0x10, LOAD)
    struct.pack_into("<I", header, 0x18, LOAD)
    struct.pack_into("<I", header, 0x1C, size)
    text = bytearray(size)
    words = {0x000: [0x27BDFFF8, 0xAFBF0004, jal(LOAD + 0x100), 0, jal(LOAD + 0x200), 0,
                     0x8FBF0004, 0x27BD0008, 0x03E00008, 0],
             0x100: [0x24020001, 0x03E00008, 0],
             0x200: [0x24020002, 0x03E00008, 0]}
    for off, ws in words.items():
        for i, w in enumerate(ws):
            struct.pack_into("<I", text, off + i * 4, w)
    return bytes(header + text)


def write_project(tmp, symbols):
    with open(os.path.join(tmp, "test.exe"), "wb") as f:
        f.write(make_psxexe())
    with open(os.path.join(tmp, "seeds.txt"), "w") as f:
        f.write("0x80010000\n")
    with open(os.path.join(tmp, "symbols.toml"), "w") as f:
        f.write(symbols)
    with open(os.path.join(tmp, "game.toml"), "w") as f:
        f.write('''[game]
name = "Symbol names test"
exe = "test.exe"
load_address = "0x80010000"
entry_pc = "0x80010000"
text_size = "0x1000"
stack_base = "0x801FFFF0"

[recompiler]
seeds = "seeds.txt"
symbols = "symbols.toml"
out_dir = "generated"
''')


SYMBOLS = '''
[[func]]
pc = 0x80010000
name = "Boot"

[[func]]
pc = "0x80010100"
name = "Helper"
status = "confirmed"

[[func]]
pc = 0x80010200
name = "OverlayOnly"
overlay = "level_10"
'''


def run(recompiler, tmp):
    return subprocess.run([recompiler, "--config", os.path.join(tmp, "game.toml")],
                          capture_output=True, text=True)


def harness(table_src):
    return table_src + r'''
#include <stdio.h>
#include <string.h>
int main(void) {
    uint32_t off = 0xFFFFFFFFu;
    const char *n;
    psx_game_symbols_register(k_psx_game_symbols,
        (uint32_t)(sizeof k_psx_game_symbols / sizeof k_psx_game_symbols[0]));
    n = psx_game_symbol_lookup(0x80010000u, &off);
    if (!n || strcmp(n, "Boot") || off != 0) return 1;
    n = psx_game_symbol_lookup(0xA0010010u, &off);          /* KSEG1 mirror */
    if (!n || strcmp(n, "Boot") || off != 0x10) return 2;
    n = psx_game_symbol_lookup(0x00010104u, &off);          /* KUSEG */
    if (!n || strcmp(n, "Helper") || off != 4) return 3;
    if (psx_game_symbol_lookup(0x80010200u, &off)) return 4; /* unnamed entry */
    if (psx_game_symbol_lookup(0x8000FFFCu, &off)) return 5;
    return 0;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--recompiler", default=os.path.join(HERE, "..", "build", "psxrecomp-game.exe"))
    ap.add_argument("--compiler")
    args = ap.parse_args()
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        write_project(tmp, SYMBOLS)
        r = run(args.recompiler, tmp)
        if r.returncode != 0:
            raise SystemExit("recompile failed:\n" + r.stdout + r.stderr)
        gen = pathlib.Path(tmp, "generated")
        full = "\n".join(p.read_text() for p in sorted(gen.glob("test.exe_full*.c")))
        dispatch = (gen / "test.exe_dispatch.c").read_text()

        if not re.search(r"^/\* Boot \*/\nvoid func_80010000\(CPUState\* cpu\)$", full, re.M):
            failures.append("func_80010000 is not labelled Boot")
        if not re.search(r"^/\* Helper \*/\nvoid func_80010100\(CPUState\* cpu\)$", full, re.M):
            failures.append("func_80010100 is not labelled Helper")
        if re.search(r"\*/\nvoid func_80010200\(CPUState\* cpu\)$", full, re.M) or "OverlayOnly" in full:
            failures.append("an overlay-keyed name labelled an EXE body")
        if re.search(r"^void (Boot|Helper)\(", full, re.M):
            failures.append("C symbols must stay func_XXXXXXXX")

        rows = re.findall(r'\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, "(\w+)"\}', dispatch)
        # Physical rows, each ending at the next compiled entry (discovery may
        # find entries in the padding after a body, so only bound the end).
        spans = [(int(lo, 16), int(hi, 16), n) for lo, hi, n in rows]
        if [(lo, n) for lo, _, n in spans] != [(0x10000, "Boot"), (0x10100, "Helper")] or \
                not (0x10028 <= spans[0][1] <= 0x10100) or \
                not (0x1010C <= spans[1][1] <= 0x10200):
            failures.append(f"name table rows: {rows}")
        if "psx_game_symbols_register(k_psx_game_symbols, 2u)" not in dispatch:
            failures.append("name table is not registered")

        if args.compiler and not failures:
            begin = dispatch.index("#include \"psx_game_symbols.h\"")
            end = dispatch.index("};\n", begin) + 3
            src = os.path.join(tmp, "names.c")
            exe = os.path.join(tmp, "names.exe")
            with open(src, "w") as f:
                f.write(harness(dispatch[begin:end]))
            subprocess.run([args.compiler, "-std=c11", "-O2",
                            "-I", os.path.join(ROOT, "runtime", "include"), src,
                            os.path.join(ROOT, "runtime", "src", "psx_game_symbols.c"),
                            "-o", exe], check=True)
            rc = subprocess.run([exe]).returncode
            if rc != 0:
                failures.append(f"runtime lookup harness failed at check {rc}")

    with tempfile.TemporaryDirectory() as tmp:
        write_project(tmp, SYMBOLS + '\n[[func]]\npc = 0x80010000\nname = "Other"\n')
        r = run(args.recompiler, tmp)
        if r.returncode == 0 or "named both" not in r.stdout + r.stderr:
            failures.append("two names for one pc did not fail the build")

    for f in failures:
        print("FAIL:", f)
    print("PASS" if not failures else f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

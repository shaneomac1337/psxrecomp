#!/usr/bin/env python3
"""[recompiler] debug_instruction_sites: guarded instruction callbacks that
exist in debug-tools builds only (docs/MOD_PACKAGES.md, "Guarded instruction
callbacks").

  - a debug site gets the same psx_mod_instruction() call as a
    mod_instruction_sites entry, inside `#ifndef PSX_NO_DEBUG_TOOLS`, so a
    build without debug tools compiles no call there;
  - mod_instruction_sites stay unconditional;
  - the debug site list is part of the overlay codegen config hash.

Usage: python test_debug_instruction_sites_codegen.py [--recompiler <psxrecomp-game>]
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


def write_project(tmp, debug_sites):
    with open(os.path.join(tmp, "test.exe"), "wb") as f:
        f.write(make_psxexe())
    with open(os.path.join(tmp, "seeds.txt"), "w") as f:
        f.write("0x80010000\n")
    sites = ", ".join(f'"{s}"' for s in debug_sites)
    with open(os.path.join(tmp, "game.toml"), "w") as f:
        f.write(f'''[game]
name = "Debug instruction sites test"
exe = "test.exe"
load_address = "0x80010000"
entry_pc = "0x80010000"
text_size = "0x1000"
stack_base = "0x801FFFF0"

[recompiler]
seeds = "seeds.txt"
out_dir = "generated"
mod_instruction_sites = ["0x80010200"]
debug_instruction_sites = [{sites}]
''')
    return os.path.join(tmp, "game.toml")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--recompiler", default=os.path.join(HERE, "..", "build", "psxrecomp-game.exe"))
    ap.add_argument("--compiler")
    args = ap.parse_args()
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        toml = write_project(tmp, ["0x80010100"])
        r = subprocess.run([args.recompiler, "--config", toml], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("recompile failed:\n" + r.stdout + r.stderr)
        gen = pathlib.Path(tmp, "generated")
        shards = sorted(gen.glob("test.exe_full*.c"))
        full = "\n".join(p.read_text() for p in shards)

        debug_call = "psx_mod_instruction(cpu, 0x80010100u, 0x24020001u);"
        mod_call = "psx_mod_instruction(cpu, 0x80010200u, 0x24020002u);"
        if not re.search(r"#ifndef PSX_NO_DEBUG_TOOLS\n\s*" + re.escape(debug_call) +
                         r"\n\s*#endif", full):
            failures.append("debug site call is missing or not guarded by PSX_NO_DEBUG_TOOLS")
        m = re.search(r"(.*)\n\s*" + re.escape(mod_call), full)
        if not m or "#ifndef PSX_NO_DEBUG_TOOLS" in m.group(1):
            failures.append("mod_instruction_sites call is missing or guarded")

        if args.compiler and not failures:
            inc = ["-I", os.path.join(ROOT, "runtime", "include"),
                   "-I", os.path.join(ROOT, "overlay_toolchain", "include"), "-I", str(gen)]
            shard = next(p for p in shards if debug_call in p.read_text())
            for defs, want in (([], True), (["-DPSX_NO_DEBUG_TOOLS=1"], False)):
                pre = subprocess.run([args.compiler, "-E", "-P", *defs, *inc, str(shard)],
                                     capture_output=True, text=True)
                if pre.returncode != 0:
                    failures.append(f"preprocess {defs}: {pre.stderr[-400:]}")
                    continue
                has = "0x80010100u, 0x24020001u" in pre.stdout
                if has != want:
                    failures.append(f"with {defs or 'debug tools'} the debug call is "
                                    f"{'present' if has else 'absent'}")

        hashes = []
        for sites in (["0x80010100"], ["0x80010100", "0x80010104"]):
            toml = write_project(tmp, sites)
            h = subprocess.run([args.recompiler, "--overlay-config-hash", toml],
                               capture_output=True, text=True)
            hashes.append(h.stdout.strip())
        if hashes[0] == hashes[1] or not hashes[0]:
            failures.append(f"debug sites do not change the overlay config hash: {hashes}")

    for f in failures:
        print("FAIL:", f)
    print("PASS" if not failures else f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

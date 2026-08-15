#!/usr/bin/env python3
"""A migration must never rename the project's executable.

psxrecomp_add_game_runtime derives the binary name from WINDOW_TITLE when no
EXE_NAME is given. The setup-host CMakeLists template carried no EXE_NAME, so
migrating a project that had pinned one silently renamed its executable — e.g.
SmackDown2Recomp.exe -> WWF_SmackDown_2_Recompiled.exe — breaking every script,
harness, and doc that referred to it, with a green build.
"""

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "new_project_layout"))

from project_studio.models import MigrateOptions           # noqa: E402
from project_studio.naming import exe_name_from_cmake      # noqa: E402
from project_studio.ops import _resolve_tokens, templates_dir  # noqa: E402

LEGACY_CMAKE = '''\
cmake_minimum_required(VERSION 3.20)
project(SmackDown2Recomp C CXX)

include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

psxrecomp_add_runtime_target(psx-runtime
    GAME_GENERATED_FULL_C     "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_012.34_full.c"
    GAME_GENERATED_DISPATCH_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_012.34_dispatch.c"
    WINDOW_TITLE              "WWF SmackDown 2 Recompiled"
    EXE_NAME                  "SmackDown2Recomp"
)
'''


def render(root: Path) -> str:
    tokens = _resolve_tokens(root, MigrateOptions())
    text = (templates_dir() / "CMakeLists.txt.in").read_text(encoding="utf-8")
    for key, value in tokens.items():
        text = text.replace("@" + key + "@", value)
    return text


def exe_name_lines(text: str) -> list[str]:
    return [ln.strip() for ln in text.splitlines() if "EXE_NAME" in ln]


class PreserveExeName(unittest.TestCase):
    def test_extracts_exe_name_from_legacy_cmake(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "CMakeLists.txt").write_text(LEGACY_CMAKE, encoding="utf-8")
            self.assertEqual(
                exe_name_from_cmake(root / "CMakeLists.txt"), "SmackDown2Recomp")

    def test_migration_keeps_the_binary_name(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "CMakeLists.txt").write_text(LEGACY_CMAKE, encoding="utf-8")
            lines = exe_name_lines(render(root))
            self.assertEqual(len(lines), 1, lines)
            self.assertEqual(lines[0], 'EXE_NAME "SmackDown2Recomp"')
            # The regression: an uncommented name derived from WINDOW_TITLE.
            self.assertNotIn("WWF_SmackDown_2_Recompiled", lines[0])

    def test_new_project_leaves_exe_name_commented(self):
        # Nothing to preserve: keep deriving from WINDOW_TITLE, but surface the
        # knob so the option is discoverable rather than invisible.
        with tempfile.TemporaryDirectory() as td:
            lines = exe_name_lines(render(Path(td)))
            self.assertEqual(len(lines), 1, lines)
            self.assertTrue(lines[0].startswith("#"), lines[0])

    def test_template_exposes_the_token(self):
        text = (templates_dir() / "CMakeLists.txt.in").read_text(encoding="utf-8")
        self.assertIn("@EXE_NAME_CMAKE_ARG@", text)


if __name__ == "__main__":
    unittest.main()

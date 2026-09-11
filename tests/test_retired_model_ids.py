#!/usr/bin/env python3
"""Hermetic tests for scripts/ci/check-retired-model-ids.py."""

from __future__ import annotations

import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_retired_model_ids", ROOT / "scripts" / "ci" / "check-retired-model-ids.py"
)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class RetiredModelIdTests(unittest.TestCase):
    def test_flags_a_retired_id_in_a_command(self) -> None:
        text = "printf '     wally opencode --cloud -m glm-5.3   code against a hosted model\\n'\n"
        self.assertEqual(CHECK.find_retired(text), [(1, "glm-5.3")])

    def test_flags_a_retired_id_ending_a_sentence(self) -> None:
        self.assertEqual(CHECK.find_retired("the model is glm-5.2."), [(1, "glm-5.2")])

    def test_flags_every_retired_id(self) -> None:
        text = "\n".join(f"-m {model_id}" for model_id in CHECK.RETIRED)
        self.assertEqual([model_id for _, model_id in CHECK.find_retired(text)], list(CHECK.RETIRED))

    def test_ignores_the_ids_that_replaced_them(self) -> None:
        text = "wally opencode --cloud -m glm-5.3-flash\nglm-5.30\nxglm-5.3\ngemini-2.5-flash-lite\n"
        self.assertEqual(CHECK.find_retired(text), [])

    def test_scan_reports_file_and_line(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "skills" / "runanywhere").mkdir(parents=True)
            (root / "skills" / "runanywhere" / "SKILL.md").write_text(
                "# RunAnywhere\n\nwally opencode --cloud -m glm-5.3\n", encoding="utf-8"
            )
            (root / "tests").mkdir()
            (root / "tests" / "fixture.py").write_text('MODEL = "glm-5.3"\n', encoding="utf-8")
            self.assertEqual(
                CHECK.scan(root),
                ["skills/runanywhere/SKILL.md:3: retired model id 'glm-5.3'"],
            )

    def test_repository_has_no_retired_ids_in_user_facing_text(self) -> None:
        self.assertEqual(CHECK.scan(), [])


if __name__ == "__main__":
    unittest.main()

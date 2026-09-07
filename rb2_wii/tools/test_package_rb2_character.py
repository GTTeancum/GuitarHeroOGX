#!/usr/bin/env python3
"""Regression tests for loose RB2 character package integrity checks."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from package_rb2_character import validate_package


class PackageValidationTests(unittest.TestCase):
    def make_package(self, root: Path) -> dict:
        relative = "char/test/og/gen/test.milo_ps2"
        payload = b"current converted model"
        target = root / "content" / Path(relative)
        target.parent.mkdir(parents=True)
        target.write_bytes(payload)
        manifest = {
            "id": "rb2.test",
            "content_root": "content",
            "content_index": "content-index.json",
            "files": [relative],
        }
        index = {
            "schema_version": 1,
            "package_id": "rb2.test",
            "files": [{
                "path": relative,
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }],
        }
        (root / "manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        (root / "content-index.json").write_text(
            json.dumps(index), encoding="utf-8"
        )
        return manifest

    def test_accepts_matching_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_package(root)
            index = validate_package(root, manifest)
            self.assertEqual(index["package_id"], "rb2.test")

    def test_rejects_stale_payload_behind_current_index(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_package(root)
            target = root / "content/char/test/og/gen/test.milo_ps2"
            target.write_bytes(b"stale converted model")
            with self.assertRaisesRegex(ValueError, "does not match content index"):
                validate_package(root, manifest)

    def test_rejects_undeclared_content(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_package(root)
            (root / "content/extra.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(ValueError, "content tree mismatch"):
                validate_package(root, manifest)


if __name__ == "__main__":
    unittest.main()

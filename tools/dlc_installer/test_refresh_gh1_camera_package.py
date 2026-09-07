import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from refresh_gh1_camera_package import publish


class CameraPublicationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="gh1-camera-publish-test-")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.package, self.candidate = root / "installed", root / "candidate"
        self.package.mkdir()
        self.candidate.mkdir()
        self.paths = ["venue.milo_ps2", "content-index.json"]
        for name in self.paths:
            (self.package / name).write_bytes(b"old:" + name.encode())
            (self.candidate / name).write_bytes(b"new:" + name.encode())
        self.original_link = root / "other-package-venue.milo_ps2"
        os.link(self.package / self.paths[0], self.original_link)

    def assert_original(self):
        for name in self.paths:
            self.assertEqual((self.package / name).read_bytes(), b"old:" + name.encode())
        self.assertEqual(self.original_link.read_bytes(), b"old:venue.milo_ps2")

    def test_publication_does_not_write_through_other_packages_hard_links(self):
        self.assertEqual(publish(self.candidate, self.package, self.paths, lambda _: "valid"), "valid")
        for name in self.paths:
            self.assertEqual((self.package / name).read_bytes(), b"new:" + name.encode())
        self.assertEqual(self.original_link.read_bytes(), b"old:venue.milo_ps2")

    def test_validation_failure_rolls_back_every_file(self):
        def reject(_):
            raise ValueError("validation rejected candidate")
        with self.assertRaises(ValueError):
            publish(self.candidate, self.package, self.paths, reject)
        self.assert_original()

    def test_partial_publication_failure_rolls_back_prior_files(self):
        replace = os.replace
        def fail_index(source, destination):
            if source == self.candidate / self.paths[1]:
                raise OSError("simulated locked index")
            return replace(source, destination)
        with patch("refresh_gh1_camera_package.os.replace", side_effect=fail_index):
            with self.assertRaises(OSError):
                publish(self.candidate, self.package, self.paths, lambda _: "valid")
        self.assert_original()


if __name__ == "__main__":
    unittest.main()

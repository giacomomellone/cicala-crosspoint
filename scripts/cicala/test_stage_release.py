"""Regression checks for refusing incompatible or incomplete release images."""

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from stage_release import stage


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.build = Path(self.directory.name) / "build"
        self.output = Path(self.directory.name) / "release"
        self.build.mkdir()
        self.provenance = {
            "fork_dirty": False,
            "core": {"dirty": False},
            "firmware_version": "0.1.0",
            "target": "cicala_release",
            "device": "x4pro",
        }
        self.image = bytearray(24)
        self.image[0] = 0xE9
        self.image[12] = 9
        self.image += b"CROSSPOINT-BOARD-V1:x4pro;"
        self.write()

    def write(self):
        (self.build / "cicala-provenance.json").write_text(json.dumps(self.provenance))
        (self.build / "firmware.bin").write_bytes(self.image)
        (self.build / "firmware.factory.bin").write_bytes(bytes(0x10000) + self.image)

    def stage(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            stage(self.build, self.output, **kwargs)

    def test_stages_pro_images_and_checksums(self):
        self.stage()
        name = "cicala-crosspoint-0.1.0-x4pro"
        self.assertEqual((self.output / (name + ".bin")).read_bytes(), self.image)
        provenance = json.loads((self.output / (name + "-provenance.json")).read_text())
        self.assertEqual(provenance["device"], "x4pro")
        self.assertIn(name + ".bin", provenance["artifacts"])
        self.assertEqual(
            len((self.output / (name + ".sha256")).read_text().splitlines()), 3
        )

    def test_refuses_c3_even_with_a_pro_tag(self):
        self.image[12] = 5
        self.write()
        with self.assertRaisesRegex(ValueError, "ESP32-S3"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_refuses_another_s3_board(self):
        self.image = self.image.replace(b"x4pro;", b"sticky;")
        self.write()
        with self.assertRaisesRegex(ValueError, "board identity"):
            self.stage()

    def test_refuses_stale_factory_image(self):
        (self.build / "firmware.factory.bin").write_bytes(bytes(0x10000))
        with self.assertRaisesRegex(ValueError, "same application"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_refuses_oversized_application(self):
        self.image.extend(bytes(6553601 - len(self.image)))
        self.write()
        with self.assertRaisesRegex(ValueError, "OTA partition"):
            self.stage()

    def test_refuses_wrong_provenance(self):
        for key, value in [
            ("target", "cicala"),
            ("device", "x4"),
            ("firmware_version", "v0.1.0"),
        ]:
            original = self.provenance[key]
            self.provenance[key] = value
            self.write()
            with self.assertRaises(ValueError):
                self.stage()
            self.provenance[key] = original

    def test_dirty_images_require_development_override(self):
        self.provenance["core"]["dirty"] = True
        self.write()
        with self.assertRaisesRegex(ValueError, "clean"):
            self.stage()
        self.stage(allow_dirty=True)


if __name__ == "__main__":
    unittest.main()

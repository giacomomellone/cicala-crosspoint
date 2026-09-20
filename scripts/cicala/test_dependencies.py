"""Check that review bootstrapping cannot mask release or transport errors."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

from dependencies import fetch_archive


class BootstrapTest(unittest.TestCase):
    def test_release_requires_the_asset(self):
        with (
            patch(
                "dependencies.urllib.request.urlretrieve",
                side_effect=HTTPError("url", 404, "missing", {}, None),
            ),
            patch("dependencies.subprocess.run") as run,
        ):
            with self.assertRaises(HTTPError):
                fetch_archive(
                    {"url": "url", "source_revision": "a" * 40},
                    Path("unused"),
                    release=True,
                )
            run.assert_not_called()

    def test_non_404_errors_never_bootstrap(self):
        for code in (403, 429, 500):
            with (
                patch(
                    "dependencies.urllib.request.urlretrieve",
                    side_effect=HTTPError("url", code, "failed", {}, None),
                ),
                patch("dependencies.subprocess.run") as run,
            ):
                with self.assertRaises(HTTPError):
                    fetch_archive(
                        {"url": "url", "source_revision": "a" * 40}, Path("unused")
                    )
                run.assert_not_called()

    def test_unpinned_sources_never_execute(self):
        for revision in ("", "main", "a" * 39, "g" * 40):
            with (
                patch(
                    "dependencies.urllib.request.urlretrieve",
                    side_effect=HTTPError("url", 404, "missing", {}, None),
                ),
                patch("dependencies.subprocess.run") as run,
            ):
                with self.assertRaises(HTTPError):
                    fetch_archive(
                        {"url": "url", "source_revision": revision}, Path("unused")
                    )
                run.assert_not_called()

    def test_missing_review_asset_builds_only_the_pinned_commit(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            patch(
                "dependencies.urllib.request.urlretrieve",
                side_effect=HTTPError("url", 404, "missing", {}, None),
            ),
            patch("dependencies.subprocess.run") as run,
        ):
            fetch_archive(
                {"url": "url", "source_revision": "a" * 40},
                Path(directory) / "core.tar.gz",
            )
            commands = [call.args[0] for call in run.call_args_list]
            self.assertIn(
                ["git", "checkout", "--quiet", "--detach", "a" * 40], commands
            )
            self.assertIn("--release", commands[-1])
            self.assertTrue(all(call.kwargs["check"] for call in run.call_args_list))


if __name__ == "__main__":
    unittest.main()

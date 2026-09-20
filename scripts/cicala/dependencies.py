"""Materialize a checked core package and patched SDK build copy for Cicala targets."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request


def fetch_archive(lock, archive, release=False):
    try:
        urllib.request.urlretrieve(lock["url"], archive)
    except urllib.error.HTTPError as error:
        revision = lock.get("source_revision", "")
        if (
            error.code != 404
            or release
            or len(revision) != 40
            or any(c not in "0123456789abcdef" for c in revision)
        ):
            raise
        # Review builds can reproduce an unpublished package from the exact
        # public source commit. The resulting archive still has to match its pin.
        with tempfile.TemporaryDirectory(dir=archive.parent) as directory:
            source = Path(directory)
            for command in [
                ["git", "init", "--quiet"],
                [
                    "git",
                    "remote",
                    "add",
                    "origin",
                    "https://github.com/giacomomellone/cicala.git",
                ],
                ["git", "fetch", "--quiet", "--depth=1", "origin", revision],
                ["git", "checkout", "--quiet", "--detach", revision],
                [
                    sys.executable,
                    "tools/package_core.py",
                    "--release",
                    "--out",
                    str(archive),
                ],
            ]:
                subprocess.run(command, cwd=source, check=True, stdout=sys.stderr)


def prepare(root):
    root = Path(root).resolve()
    lock = json.loads((root / "cicala-core.lock.json").read_text())
    local_archive = os.environ.get("CICALA_CORE_ARCHIVE")
    cache = root / ".cache/cicala"
    cache.mkdir(parents=True, exist_ok=True)
    archive = Path(local_archive).resolve() if local_archive else cache / "core.tar.gz"
    if not local_archive and (
        os.environ.get("CICALA_RELEASE_BUILD")
        or not archive.exists()
        or hashlib.sha256(archive.read_bytes()).hexdigest() != lock["sha256"]
    ):
        fetch_archive(lock, archive, bool(os.environ.get("CICALA_RELEASE_BUILD")))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if (not local_archive or os.environ.get("CICALA_RELEASE_BUILD")) and digest != lock[
        "sha256"
    ]:
        raise RuntimeError("Cicala core package checksum mismatch")
    core = cache / ("core-" + digest)
    if not core.exists():
        core.mkdir()
        try:
            with tarfile.open(archive) as package:
                for entry in package.getmembers():
                    if (
                        not entry.isfile()
                        or Path(entry.name).is_absolute()
                        or ".." in Path(entry.name).parts
                    ):
                        raise RuntimeError("Invalid core package member")
                    target = core / entry.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(package.extractfile(entry).read())
        except Exception:
            shutil.rmtree(core)
            raise

    sdk = root / "freeink-sdk"
    sdk_revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=sdk, text=True
    ).strip()
    if sdk_revision != lock["sdk_revision"]:
        raise RuntimeError(
            "Update and test the checked-refresh patch before adopting a new SDK revision"
        )
    if subprocess.run(["git", "diff", "--quiet", "HEAD"], cwd=sdk).returncode:
        raise RuntimeError(
            "The pinned SDK worktree must be clean; use a reviewed patch"
        )
    patch = root / "patches/freeink-checked-refresh.patch"
    patch_hash = hashlib.sha256(patch.read_bytes()).hexdigest()
    display = cache / ("display-" + sdk_revision + "-" + patch_hash)
    if not display.exists():
        shutil.copytree(sdk / "libs/display/FreeInkDisplay", display)
        try:
            subprocess.run(
                ["git", "apply", "--check", str(patch)], cwd=display, check=True
            )
            subprocess.run(["git", "apply", str(patch)], cwd=display, check=True)
        except Exception:
            shutil.rmtree(display)
            raise

    return core, display, digest, sdk_revision


if __name__ == "__main__":
    print(prepare(Path(__file__).resolve().parents[2])[0])

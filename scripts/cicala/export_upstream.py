#!/usr/bin/env python3
"""Export and apply-check generic changes without changing the working tree or index."""

import difflib
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
base = json.loads((root / "cicala-core.lock.json").read_text())["upstream_revision"]
out = root / "build/upstream"
out.mkdir(parents=True, exist_ok=True)


def original(path):
    result = subprocess.run(
        ["git", "show", f"{base}:{path}"], cwd=root, capture_output=True, text=True
    )
    return result.stdout if result.returncode == 0 else ""


def write_patch(name, files):
    patch = ""
    for path, content in files.items():
        old = original(path)
        patch += "".join(
            difflib.unified_diff(
                old.splitlines(True),
                content.splitlines(True),
                fromfile="a/" + path if old else "/dev/null",
                tofile="b/" + path,
            )
        )
    destination = out / name
    destination.write_text(patch)
    with tempfile.TemporaryDirectory() as directory:
        environment = {**os.environ, "GIT_INDEX_FILE": str(Path(directory) / "index")}
        subprocess.run(
            ["git", "read-tree", base], cwd=root, env=environment, check=True
        )
        subprocess.run(
            ["git", "apply", "--cached", "--check", str(destination)],
            cwd=root,
            env=environment,
            check=True,
        )
    print(destination.relative_to(root))


ota_paths = [
    "src/network/OtaConfig.h",
    "src/network/OtaRelease.h",
    "src/network/OtaUpdater.h",
    "src/network/OtaUpdater.cpp",
    "src/activities/settings/OtaUpdateActivity.cpp",
    "test/ota_release/CMakeLists.txt",
    "test/ota_release/OtaReleaseTest.cpp",
]
ota = {path: (root / path).read_text() for path in ota_paths}
ota["test/CMakeLists.txt"] = (
    original("test/CMakeLists.txt") + "\nadd_subdirectory(ota_release)\n"
)
write_patch("crosspoint-configurable-ota.patch", ota)

write_patch(
    "crosspoint-checked-display.patch",
    {
        path: (root / path).read_text()
        for path in [
            "lib/hal/HalDisplay.h",
            "lib/GfxRenderer/GfxRenderer.h",
            "lib/GfxRenderer/GfxRenderer.cpp",
        ]
    },
)

manager = "src/activities/ActivityManager.cpp"
current = (root / manager).read_text()
start = current.index("bool ActivityManager::prepareForSleep()")
end = current.index("\n}\n", start) + 3
header = "src/activities/ActivityManager.h"
sleep = original("src/main.cpp").replace(
    "  activityManager.goToSleep(fromTimeout);\n\n  if (isQuickResumeSleep) {",
    "  const bool preserveScreen = activityManager.prepareForSleep();\n"
    "  if (!preserveScreen) activityManager.goToSleep(fromTimeout);\n\n"
    "  if (isQuickResumeSleep && !preserveScreen) {",
)
write_patch(
    "crosspoint-activity-sleep.patch",
    {
        "src/activities/Activity.h": (root / "src/activities/Activity.h").read_text(),
        manager: original(manager) + "\n" + current[start:end],
        header: original(header).replace(
            "  void goToSleep(bool fromTimeout = false);",
            "  void goToSleep(bool fromTimeout = false);\n  bool prepareForSleep();",
        ),
        "src/main.cpp": sleep,
    },
)
(out / "freeink-checked-refresh.patch").write_bytes(
    (root / "patches/freeink-checked-refresh.patch").read_bytes()
)
(out / "base.json").write_text(
    json.dumps(
        {
            "upstream_revision": base,
            "sdk_revision": json.loads((root / "cicala-core.lock.json").read_text())[
                "sdk_revision"
            ],
        },
        indent=2,
    )
    + "\n"
)

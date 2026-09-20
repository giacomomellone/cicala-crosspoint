"""Configure the PlatformIO target from immutable Cicala dependencies."""

import importlib.util
import json
from pathlib import Path
import subprocess

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
spec = importlib.util.spec_from_file_location(
    "cicala_dependencies", root / "scripts/cicala/dependencies.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
core, display, digest, sdk_revision = module.prepare(root)
lock = json.loads((root / "cicala-core.lock.json").read_text())
env.Append(
    CPPDEFINES=[
        ("CICALA_UPSTREAM_REVISION", '\\"' + lock["upstream_revision"][:7] + '\\"')
    ]
)
provenance = {
    "fork_revision": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip(),
    "fork_dirty": bool(
        subprocess.check_output(["git", "status", "--porcelain"], cwd=root)
    ),
    "upstream_revision": lock["upstream_revision"],
    "sdk_revision": sdk_revision,
    "core_sha256": digest,
    "core": json.loads((core / "package-provenance.json").read_text()),
    "target": env.subst("$PIOENV"),
    "device": env.GetProjectOption("custom_cicala_board"),
    "firmware_version": env.GetProjectConfig().get("cicala", "version"),
    "upstream_version": env.GetProjectConfig().get("crosspoint", "version"),
}
build = Path(env.subst("$BUILD_DIR"))
build.mkdir(parents=True, exist_ok=True)
(build / "cicala-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
dependencies = [
    item
    for item in env.GetProjectOption("lib_deps")
    if not item.startswith("EInkDisplay=")
]
dependencies.extend(
    ["EInkDisplay=symlink://" + str(display), "cicala-core=symlink://" + str(core)]
)
env.Replace(LIB_DEPS=dependencies)
env.Append(CPPPATH=[str(core / "include")])
# PlatformIO resolves dependencies before building source; override the project
# option as well as the SCons environment for its library discovery pass.
env.GetProjectConfig().set(
    "env:" + env.subst("$PIOENV"), "lib_deps", "\n".join(dependencies)
)

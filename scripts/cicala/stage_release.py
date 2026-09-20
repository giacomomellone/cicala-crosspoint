#!/usr/bin/env python3
"""Validate and stage local X4 Pro release artifacts without publishing anything."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def stage(build: Path, output: Path, allow_dirty: bool = False):
    provenance = json.loads((build / "cicala-provenance.json").read_text())
    if not allow_dirty and (provenance["fork_dirty"] or provenance["core"]["dirty"]):
        raise ValueError("Release artifacts require clean fork and core sources")
    version = provenance["firmware_version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("A stable numeric firmware version is required")
    if provenance["target"] != "cicala_release":
        raise ValueError("Build the cicala_release target first")
    if provenance.get("device") != "x4pro":
        raise ValueError("Expected X4 Pro build provenance")
    firmware = (build / "firmware.bin").read_bytes()
    if (
        len(firmware) < 24
        or firmware[0] != 0xE9
        or int.from_bytes(firmware[12:14], "little") != 9
    ):
        raise ValueError("Expected an ESP32-S3 application image")
    if b"CROSSPOINT-BOARD-V1:x4pro;" not in firmware:
        raise ValueError("Expected the X4 Pro board identity")
    if len(firmware) > 6553600:
        raise ValueError("Image exceeds the OTA partition")
    factory = (build / "firmware.factory.bin").read_bytes()
    if factory[0x10000 : 0x10000 + len(firmware)] != firmware:
        raise ValueError("Factory image must contain the same application at 0x10000")
    prefix = f"cicala-crosspoint-{version}-x4pro"
    output.mkdir(parents=True, exist_ok=True)
    artifacts = []
    for source, name in [
        ("firmware.bin", prefix + ".bin"),
        ("firmware.factory.bin", prefix + "-factory.bin"),
    ]:
        destination = output / name
        shutil.copyfile(build / source, destination)
        artifacts.append(destination)
    provenance["artifacts"] = {
        p.name: {
            "bytes": p.stat().st_size,
            "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
        }
        for p in artifacts
    }
    record = output / (prefix + "-provenance.json")
    record.write_text(json.dumps(provenance, indent=2) + "\n")
    artifacts.append(record)
    (output / (prefix + ".sha256")).write_text(
        "".join(
            f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
            for p in artifacts
        )
    )
    print(output.resolve())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path(".pio/build/cicala_release"))
    parser.add_argument("--out", type=Path, default=Path("build/cicala-release"))
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Stage development images with their dirty provenance",
    )
    args = parser.parse_args()
    stage(args.build, args.out, args.allow_dirty)

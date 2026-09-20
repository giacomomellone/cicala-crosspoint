#!/usr/bin/env python3
"""Run the production CicalaStore against native SD/HTTP adapters and signed fixtures."""

import base64
from functools import partial
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from threading import Thread


def openssl():
    for path in [
        os.getenv("OPENSSL"),
        shutil.which("openssl"),
        "/opt/homebrew/opt/openssl@3/bin/openssl",
        "/usr/local/opt/openssl@3/bin/openssl",
    ]:
        if (
            path
            and Path(path).is_file()
            and subprocess.check_output([path, "version"]).startswith(b"OpenSSL ")
        ):
            return path
    raise RuntimeError("OpenSSL 3 is required; set OPENSSL to its executable")


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


def main():
    executable = str(Path(sys.argv[1]).resolve())
    original = Path(sys.argv[2]).read_bytes()
    crypto = openssl()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        web = root / "http"
        web.mkdir()
        sd = root / "sd"
        sd.mkdir()
        key = root / "key.pem"
        subprocess.run(
            [crypto, "genpkey", "-algorithm", "ed25519", "-out", str(key)], check=True
        )
        public = subprocess.check_output(
            [crypto, "pkey", "-in", str(key), "-pubout", "-outform", "DER"]
        )
        public_path = root / "public.bin"
        public_path.write_bytes(public[-32:])
        server = ThreadingHTTPServer(
            ("127.0.0.1", 0), partial(Handler, directory=str(web))
        )
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        environment = {**os.environ, "CICALA_TEST_URL": base + "/manifest.json"}

        def fixture(version="1.0.0", transform=None):
            raw = (
                b"QDB4"
                + bytes([len(version)])
                + version.encode()
                + original[5 + original[4] :]
            )
            if transform:
                raw = transform(raw)
            (web / "bundle.qdb").write_bytes(raw)
            digest = hashlib.sha256(raw).digest()
            (root / "digest").write_bytes(digest)
            signature = subprocess.check_output(
                [
                    crypto,
                    "pkeyutl",
                    "-sign",
                    "-inkey",
                    str(key),
                    "-rawin",
                    "-in",
                    str(root / "digest"),
                ]
            )
            count_offset = 5 + raw[4]
            count_offset += 1 + raw[count_offset]
            entry = {
                "url": base + "/bundle.qdb",
                "size": len(raw),
                "count": int.from_bytes(raw[count_offset : count_offset + 2], "little"),
                "sha256": digest.hex(),
                "sig": base64.b64encode(signature).decode(),
            }
            return {
                "schema": 4,
                "version": version,
                "min_fw": "0.2.0",
                "languages": {"en": entry},
            }

        def run(manifest, result, version, operation="sync", budget=0):
            (web / "manifest.json").write_text(json.dumps(manifest))
            subprocess.run(
                [
                    executable,
                    str(sd),
                    str(public_path),
                    operation,
                    str(result),
                    version,
                    str(budget),
                ],
                env=environment,
                check=True,
            )

        try:
            manifest = fixture()
            manifest["languages"]["en"]["sig"] = None
            run(manifest, 2, "0.0.0")
            run(fixture(), 0, "1.0.0")
            run(fixture(), 1, "1.0.0")
            run(fixture("0.9.0"), 1, "1.0.0")

            for field, value in [
                ("size", 32769),
                ("count", 513),
                ("count", 0),
                ("sig", base64.b64encode(bytes(64)).decode()),
            ]:
                manifest = fixture("1.1.0")
                manifest["languages"]["en"][field] = value
                run(manifest, 2, "1.0.0")
            manifest = fixture()
            manifest["version"] = "1.1.0"
            run(manifest, 2, "1.0.0")
            manifest = fixture("1.1.0")
            manifest["min_fw"] = "99.0.0"
            run(manifest, 2, "1.0.0")
            run(fixture("1.1.0", lambda raw: raw[:-1] + b"\0"), 2, "1.0.0")
            run(fixture("1.1.0", lambda raw: raw[:-1] + b"\xff"), 2, "1.0.0")
            manifest = fixture("1.1.0")
            (web / "bundle.qdb").write_bytes((web / "bundle.qdb").read_bytes()[:-1])
            run(manifest, 2, "1.0.0")
            manifest = fixture("1.1.0")
            (web / "bundle.qdb").unlink()
            run(manifest, 2, "1.0.0")
            for budget in [0, len(original) - 1, len(original) + 5]:
                run(fixture("1.1.0"), 2, "1.0.0", "short-write", budget)
            run(fixture("1.1.0"), 3, "1.0.0", "cancel")
            run(fixture("1.1.0"), 0, "1.1.0")
            (sd / ".crosspoint/cicala/bundle-1.qdb").write_bytes(b"torn")
            run(fixture("1.1.0"), 3, "1.0.0", "cancel")
            (sd / ".crosspoint/cicala/bundle-0.qdb").write_bytes(b"torn")
            run(fixture("1.1.0"), 3, "0.0.0", "cancel")
            subprocess.run(
                [executable, str(root / "snapshot-sd"), str(public_path), "snapshots"],
                check=True,
            )
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
    print(
        "CicalaStore: signed HTTP updates, invalid payloads, torn slots, cancellation and SD recovery passed"
    )


if __name__ == "__main__":
    main()

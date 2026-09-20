# Cicala on Xteink X4 Pro

This fork adds Cicala to CrossPoint's Home menu. The target is Xteink X4 Pro,
with English questions, manual bundle updates, and sessions saved on SD. The
core and corpus belong to the public
[Cicala repository](https://github.com/giacomomellone/cicala).

The ESP32-S3 firmware builds and its host suites pass. On-device verification remains
required before release. The current public English corpus is empty, so the
compiled fallback shows the empty-deck message. Test fixtures never become the
product deck.

## Build

Use Python 3.13 and a recursive clone. The pinned pioarduino core is needed for
the SDK configuration inherited from CrossPoint.

```sh
git clone --recursive https://github.com/giacomomellone/cicala-crosspoint.git
cd cicala-crosspoint
git switch cicala
python3.13 -m venv .venv
.venv/bin/pip install -r scripts/cicala/requirements.txt
.venv/bin/pio run -e cicala
```

`cicala` and `cicala_release` both inherit the upstream Pro board profile:
ESP32-S3, 8 MB PSRAM, native SDMMC, USB file transfer, frontlight, touch, and
runtime SSD1677/UC8179/UC8279 display detection. These images are specific to
the Pro.

`cicala-core.lock.json` pins the public core archive, its SHA-256, its source
commit, the SDK revision, and the adopted upstream revision. The pre-build script checks the
archive, rejects a changed SDK checkout, and applies
`patches/freeink-checked-refresh.patch` to an ignored display-library copy under
`.cache/cicala/`. It never patches the SDK submodule in place.

If the release asset returns 404, review builds fetch the pinned public source
commit and reproduce the archive with Python 3.13, then require the same SHA-256.
`CICALA_RELEASE_BUILD=1` disables this bootstrap and requires the published
asset. Other HTTP errors and checksum mismatches fail.

For local core edits, generate the package in a public Cicala checkout with
`just core-package`, then supply its absolute path:

```sh
export CICALA_CORE_ARCHIVE=/absolute/path/to/cicala/dist/cicala-core-0.1.0.tar.gz
.venv/bin/pio run -e cicala
```

The override supports local development and records the actual package hash.
The default release URL is a rollout dependency; a local build does not publish
it. The published package must match the pinned source revision and checksum. A clean package has different provenance from a development
package made in a dirty working tree.

For initial USB installation use `.venv/bin/pio run -e cicala -t upload` and the
existing CrossPoint flashing procedure. Preserve books and existing SD contents.
The application binary is for OTA; `firmware.factory.bin` includes the
bootloader and partition table for initial installation.

## Controls and session lifetime

| Context  | Logical control          | Result                                            |
| -------- | ------------------------ | ------------------------------------------------- |
| Question | Filters / Page Back      | Open the draft filter menu                        |
| Question | Next / Page Forward      | Draw the next eligible question                   |
| Filters  | Filters / Page Back      | Move to the next row                              |
| Filters  | Next / Page Forward      | Toggle the row, or apply Done                     |
| Question | Confirm / Menu           | Open Resume, New session, Update questions, About, Home |
| Options  | Previous / Next, Confirm | Choose and activate an option                     |
| Filters  | Back / Cancel            | Discard the draft and restore the same question   |
| Filters  | Confirm / Apply          | Commit the draft                                 |
| Question | Back                     | Return to Home and save the session               |

The Reading room layout uses centered Noto Sans 18 pt questions, with a 14 pt
fallback for text that cannot fit. The question header shows a 36 px Cicala
symbol alone. Home shows a smaller 32 px symbol beside the name in all three
themes; the Cicala Menu and Filters headers use that same smaller size.

On the Pro, the bottom touch buttons expose Menu, Filters, and a black Next
button with white text. Tap a menu row to activate it. Filters has three
permission rows and separate Cancel / Apply controls. A new session requires
confirmation, initially focused on keeping the current session. The two
physical page buttons provide Filters/Next during play and previous/next
selection in Menu. CrossPoint handles the configured Home
key and frontlight gestures. Button mapping and live orientation apply.
Contacts begun during a refresh are discarded through their release, including
a finger held down until the refresh finishes. A
question enters the seen history only after a matching successful display
result. Filters apply on Apply (or the selected Done position with a page button).
Cancel also uses the core's render-commit transaction: a failed refresh retains
the open draft. A confirmed new session clears permissions and history.

Status and version text wraps within the available area. An empty collection
offers a question update; an empty eligible pool instead keeps Filters available.
The layout derives its bounds from the current orientation and viewable panel
margins. Menu rows reflow into two columns in landscape. The host layout tests
check that controls and rows remain inside the panel without overlapping.

`src/cicala/CicalaLayout.h` owns Cicala geometry; `CicalaActivity` owns rendering
and input. Logos use 308 bytes of constant bitmap data, generated from
`assets/cicala/mark.svg` by `python3 scripts/cicala/generate_logo.py`; run
`./bin/clang-format-fix -g` after regeneration. The activity reuses bounded
member buffers for wrapping, including the longer version/status strings.
No wrapping or logo operation allocates a buffer per frame.

Sleep keeps the committed question or filter view visible with a wake instruction
in place of the touch footer. Normal power-off loses
RAM, so the session lives under `/.crosspoint/cicala/`. Reopening after reading a
book restores it. Sleep writes a one-shot resume marker; the next boot opens
Cicala. Holding the right page button (Down) at boot bypasses Cicala resume; a power-button
wake with Down held uses CrossPoint's firmware recovery. A panic takes the crash
report route. The left page button is GPIO0, the ESP32-S3 boot strap; use the
right page button for recovery.
Unexpected power loss can roll back to the last save. Saves occur on sleep,
activity exit, and successful New session, rather than on every button press.

`session-0.bin` and `session-1.bin` alternate generations. The fixed-width
little-endian codec checks its magic, size, capacity fields, values, and checksum.
It rejects a torn newest record and can restore the preceding generation.
Reading a snapshot never exposes pointers into the SD file or a bundle buffer.

## Question and firmware updates

Update questions opens CrossPoint's Wi-Fi selector. A cancellable worker streams
the manifest and payload, checks the stored bytes, then writes the inactive
slot's manifest as its commit record. Limits are 2 KiB of manifest, 32 KiB of
corpus, 512 questions, and 128 UTF-8 bytes per question. Every activated or
reopened SD bundle passes SHA-256, Ed25519, structure, metadata/header agreement,
and text checks. Invalid slots fall back to the compiled English deck.

The copied question and filter state survive an update. Selection indices reset
when the corpus fingerprint changes. A connection opened by Cicala is disabled
after completion or cancellation. Normal play performs no network activity.

The default manifest is `http://device.cicala.dev/device/manifest.json`.
Signatures authenticate bundles independently of HTTP. The hostname did not
resolve from the development host during implementation; verify the live signed
feed before release. A test build can override `CICALA_MANIFEST_URL`; production
firmware retains the public key from the core package.

Settings → Update firmware uses the fork's GitHub releases. Stable tags have
three numeric components, starting with `0.1.0`. OTA selects exactly
`cicala-crosspoint-<version>-x4pro.bin`, validates the byte count, and retains
CrossPoint's chip/board guards and inactive-partition flashing path. The image
contains both CrossPoint and Cicala. The About page inside Cicala shows the fork,
upstream, core, and bundle versions separately.

## Native checks

Install OpenSSL, libcurl, and Expat development headers. On macOS Homebrew
OpenSSL 3 works with `-DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"`; Ubuntu
uses `libssl-dev libcurl4-openssl-dev libexpat1-dev`. CMake fetches the same
GoogleTest version as the existing CrossPoint test harness.

```sh
core="$(.venv/bin/python scripts/cicala/dependencies.py)"
cmake -S test -B build/native -DCICALA_CORE_DIR="$core" -DCICALA_BUILD_TESTS=ON
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure --parallel 2
```

The Cicala store test compiles the production store and packaged core with
native filesystem, SHA-256, and HTTP adapters. A temporary localhost server
serves real QDB4 fixtures signed by a temporary test key. It covers updates,
replay, bad signatures, mismatched headers, invalid text, download failures,
short SD writes, corrupt slots, cancellation, session recovery, and the resume
marker. It does not emulate ESP-IDF's TLS stack or FAT power-loss behavior.

The checked-refresh suite compiles the patched SDK bus with simulated BUSY
edges, GPIO, and semaphore waits. SSD1677 requires a BUSY assertion and
completion. The Pro's UltraChip variants use the SDK's one-tick then idle-HIGH
completion rule, with a timeout for stuck LOW during checked refreshes. This
level-based rule cannot distinguish a disconnected line that reads HIGH from
an idle controller. Physical panel timing still needs device verification. The core also runs shared TypeScript/C++ selection fixtures in
the public Cicala repository.

## Release and upstream maintenance

```sh
.venv/bin/pio run -e cicala_release
.venv/bin/python scripts/cicala/stage_release.py --allow-dirty
```

This stages development artifacts under `build/cicala-release/`, including
application and factory images, SHA-256 checksums, and provenance. Staging
checks the ESP32-S3 chip ID, `x4pro` board tag, OTA partition limit, target/device
provenance, and the factory image's embedded application. Without
`--allow-dirty`, staging rejects dirty fork or core sources. Staging never
publishes. A release uses the clean pinned public package and the same target.

The `Cicala X4 Pro release` workflow attaches checked artifacts when a maintainer
publishes a stable release matching `[cicala].version`. Inherited CrossPoint
release and release-candidate workflows are restricted to the upstream
repository. Bump the fork version for Cicala releases, even when the underlying
CrossPoint version stays the same.

Keep the fork's `develop` branch aligned with upstream. Merge upstream into
`cicala`; use rebases for unpublished feature branches. Preserve book-related
changes in their own commits and keep Cicala-specific code in `src/cicala/`,
`src/activities/cicala/`, and `platformio.cicala.ini`.

The daily Cicala workflow tests an uncommitted merge of upstream `develop` in
its disposable checkout. It runs native suites and builds X4 Pro without pushing,
opening PRs, or publishing. Set `cicala` as the fork's default branch after
publishing it so scheduled checks run on the product branch. When adopting an
upstream merge, update `upstream_revision` in the lock. If the SDK pin changes,
review and test the display patch before updating `sdk_revision`.

Generic contributions are separated in [upstream changes](upstream_changes.md).
They are local review material; no upstream PR is submitted by these scripts.

## Device release checks

1. Record the detected panel controller (SSD1677, UC8179, or UC8279). Boot X4 Pro, open Cicala, exercise all eight permission combinations, and check
   long UTF-8 questions in portrait and landscape with each available theme, including taps at row edges and rotated coordinates.
2. Hold and tap controls during full and partial refreshes. Verify that no
   queued release consumes a question. Check ghosting across twenty draws.
3. Sleep from a question, filters, and options; confirm the retained image and
   session after wake. Read a book and return. Exercise New session and the right page button
   held at boot. Verify normal CrossPoint book progress and sleep behavior.
4. Record free heap and largest allocatable block during play, Wi-Fi selection,
   HTTPS, and signature verification. Check cancellation and low-memory errors.
   Static image size alone does not establish the runtime RAM margin.
5. Exercise a signed update, offline/error response, truncated download,
   read-only SD, and interrupted inactive-slot write. Check both-slot corruption
   and fallback. Measure sleep current on the X4 Pro itself.
6. Install a newer fork build by OTA while its upstream version stays constant.
   Check rejection of an older version and wrong-board image. Verify the live
   signed question feed and the clean public core package before publishing.

# Upstream contribution boundaries

The fork keeps the following changes suitable for separate review. Export local
patches with `python scripts/cicala/export_upstream.py`; the script does not
commit, push, or submit PRs.

## Configurable firmware releases

`src/network/OtaConfig.h`, `OtaRelease.h`, and `OtaUpdater.cpp` provide default
CrossPoint repository/version/asset values that a downstream firmware can
override. The parser rejects malformed stable tags and oversized asset names;
RC-to-stable comparisons retain upstream behavior. Starting a new check clears
old results, and flashing checks the advertised byte count. The update UI reads
the updater's current version. `test/ota_release/` exercises version ordering
and board asset naming.

The stock defaults remain in the generic code. Cicala values live in
`platformio.cicala.ini`. The exported patch excludes that target and Cicala's UI.

## Checked display completion

`patches/freeink-checked-refresh.patch` targets the FreeInk SDK revision in
`cicala-core.lock.json`. It adds an opt-in checked operation result while
retaining existing blocking display methods. The bus latches a failed BUSY
wait; the facade rejects unavailable frame/driver state and requests resync
after a timeout. `test/checked_refresh/` tests the real patched bus with native
GPIO and RTOS shims: SSD1677 assertion/completion, missing and stuck BUSY,
and the Pro UltraChip idle-HIGH protocol and checked timeout. The latter
retains the SDK's level-based rule and cannot diagnose a line stuck HIGH.

CrossPoint's `HalDisplay` and `GfxRenderer` expose the checked method when the
SDK provides it. A contribution to the SDK can be reviewed first, then the HAL
wrapper can follow after the SDK dependency is updated. Cicala builds the patch
into a copied display library until those changes are accepted upstream.

## Activity sleep hooks

`Activity::prepareForSleep()` lets an activity stop workers and persist state
before storage and device power shut down. `preserveScreenOnSleep()` allows an
activity to keep its committed content on screen. Default implementations leave
the existing sleep activity in control. The manager serializes preparation
with rendering and also prepares suspended parent activities.

These hooks, the checked display wrapper, and the OTA configuration are the
generic extension points. The Home-menu entry, resume marker, Cicala activity,
SD slot format, bundle worker, and product release channel stay in the fork.

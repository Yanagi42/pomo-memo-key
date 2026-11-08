# 03: BLE remote control

**Effort:** pose-driven-ui
**Blocked by:** 01
**Files:** `src/main.c`, `src/ble_hid.c`, `capsule/src/capsule.h`, `capsule/src/capsule.c`, `capsule/tests/test_capsule.c`, `project/proj.conf`, `docs/01-需求规格说明.md`, `docs/03-测试计划.md`
**Read first:** Spec `pose-driven-ui.md`; REQ-005; SDK v2.5.1 BLE HID Consumer Control example
**Check:** Deferred by user request. After all `pose-driven-ui` implementation tasks are complete, the user will perform unified manual acceptance; until then do not run tests, smoke checks, or builds.
**Covers:** W4
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Use BLE HID Consumer Control for volume up, next track, and system audio mute.
- [x] Preserve side-dependent KEY2 short-press mappings and the 600 ms long-press threshold; use system audio mute semantics.
- [x] Add a functional side-remote screen with touch controls for the same supported actions.
- [x] Keep local timer, recording, and screen operation available while BLE is disconnected.

## Progress
- Check before: deferred; user requested no development-stage tests, smoke tests, or builds. Unified manual acceptance will happen after all effort specifications are implemented.
- Check after: SDK Consumer Control HID service, physical macro dispatch, side-page touch controls, BLE-disconnected no-op behavior, and local feature paths implemented. No tests, build, smoke check, or board operation run per user instruction; unified manual acceptance remains pending.

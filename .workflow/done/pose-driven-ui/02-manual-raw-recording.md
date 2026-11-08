# 02: Manual raw-audio recording

**Effort:** pose-driven-ui
**Blocked by:** 01
**Files:** `capsule/src/capsule.c`, `capsule/src/capsule.h`, `capsule/tests/test_capsule.c`, `capsule/Makefile`, `capsule/src/selftest.c`, `src/main.c`, `docs/01-需求规格说明.md`, `docs/02-方案设计说明.md`, `docs/03-测试计划.md`, `capsule/README.md`
**Read first:** Spec `pose-driven-ui.md`; REQ-004; current capture buffers, key path, and LVGL command queue
**Check:** Deferred by user request. After all `pose-driven-ui` implementation tasks are complete, the user will perform unified manual acceptance; until then do not run tests, smoke checks, or builds.
**Covers:** W1
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Make recording start and stop explicit user actions; support the flat-page touch control and KEY2 shortcut.
- [x] Preserve the full captured PCM in WAV; remove VAD trimming and obsolete VAD API/implementation/tests.
- [x] Retain buffer-capacity reporting and avoid treating a full buffer as a successfully completed recording.
- [x] Update the recording design and tests to state that silence is intentionally retained.

## Progress
- Check before: deferred; user requested no development-stage tests, smoke tests, or builds. Unified manual acceptance will happen after all effort specifications are implemented.
- [x] Supersede the paused `adaptive-vad/01` queue item; its adaptive-noise-gate behavior is out of scope.
- Check after: manual start/stop APIs, full PCM WAV writing, explicit `CAPSULE_MEMO_NOSPACE` on truncation, KEY2 integration, and flat-page touch control implemented. No tests, build, smoke check, or board operation run per user instruction; unified manual acceptance remains pending.

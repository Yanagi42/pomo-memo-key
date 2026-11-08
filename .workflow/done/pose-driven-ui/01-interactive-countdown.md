# 01: Interactive countdown modes

**Effort:** pose-driven-ui
**Blocked by:** None
**Files:** `capsule/src/capsule.c`, `capsule/src/capsule.h`, `capsule/tests/test_capsule.c`, `src/main.c`, `project/proj.conf`
**Read first:** Spec `pose-driven-ui.md`; REQ-002/003/006; current timer state and LVGL task
**Check:** Deferred by user request. After all `pose-driven-ui` implementation tasks are complete, the user will perform unified manual acceptance; until then do not run tests, smoke checks, or builds.
**Covers:** W1, W2
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Make Pomodoro and Countdown Reminder selectable countdown modes with identical countdown semantics and independent RAM-held time values.
- [x] Start on stable upright, pause when leaving upright, resume on upright, and do not auto-restart after completion until leaving and returning.
- [x] Support 5–120 minutes in 5-minute steps, default each mode to 25 minutes, and apply preset deltas to the current remaining time while running.
- [x] Add the end-sound toggle and touch controls to the timer screen.

## Progress
- Check before: deferred; user requested no development-stage tests, smoke tests, or builds. Unified manual acceptance will happen after all effort specifications are implemented.
- [x] Enable the SDK FT6146 touch device without changing the IMU’s I2C1 pinmux; defer physical coexistence verification to unified manual acceptance.
- Check after: timer modes, preset adjustment API, finish-sound control, LVGL touch controls, and FT6146 project configuration implemented. No tests, build, smoke check, or board operation run per user instruction; all behavior remains unverified pending unified manual acceptance.

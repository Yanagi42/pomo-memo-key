# 05: Pose-driven spatial screen transitions

**Effort:** pose-driven-ui
**Blocked by:** 03, 04
**Files:** `src/main.c`, `project/proj.conf`, `docs/02-方案设计说明.md`, `docs/03-测试计划.md`
**Read first:** Spec `pose-driven-ui.md`; REQ-006; SDK LVGL v8 and FT6146 touch driver integration
**Check:** Deferred by user request. After all `pose-driven-ui` implementation tasks are complete, the user will perform unified manual acceptance; until then do not run tests, smoke checks, or builds.
**Covers:** W2
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Map upright to timer, both side poses to the same remote controls, flat to recording, and face-down to hidden display content.
- [x] Place timer in center, left/right remote views in horizontal neighbor panels, and recorder below; use eased slide transition.
- [x] Retarget an in-flight animation from its current position to the newest stable pose; never queue stale pose targets.
- [x] Keep current page on unknown pose and preserve existing timer pause behavior.
- [x] Keep the IMU I2C1 pinmux unchanged while enabling touch; defer physical concurrency validation to unified acceptance.

## Progress
- Check before: deferred; user requested no development-stage tests, smoke tests, or builds. Unified manual acceptance will happen after all effort specifications are implemented.
- Check after: 3×3 timer/remote/recording panels and face-down hiding implemented; animation retargets current page position toward the latest stable pose; unknown pose leaves page unchanged. No tests, build, smoke check, or board operation run per user instruction; physical touch/IMU coexistence remains pending unified manual acceptance.

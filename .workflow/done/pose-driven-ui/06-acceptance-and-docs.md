# 06: Full acceptance and documentation

**Effort:** pose-driven-ui
**Blocked by:** 05
**Files:** `docs/00-项目计划与里程碑.md`, `docs/01-需求规格说明.md`, `docs/02-方案设计说明.md`, `docs/03-测试计划.md`, `docs/04-测试报告.md`, `docs/06-风险与问题台账.md`, `README.md`, `evidence/`
**Read first:** Spec `pose-driven-ui.md`; all completed task checks; existing board evidence and course acceptance requirements
**Check:** Host tests and W1–W5 physical feature acceptance remained deferred at task close. The user later requested a firmware build, flash, and UART reset; see `evidence/2026-11-06-firmware-flash.txt` and `evidence/2026-11-06-serial-reset-boot.txt`.
**Covers:** W1, W2, W3, W4, W5
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Update requirements and design documents to reflect identical countdown semantics for both modes, raw manual recording, TF persistence/fallback, HID mappings, and pose-driven screens.
- [x] Update test plan and report with host and board procedures/results; do not mark unexecuted hardware checks as passed.
- [x] Update risk ledger, milestone status, and README to match verified firmware behavior and TF-card dependency.
- [x] Full host suite and W1–W5 physical feature acceptance remain deferred by explicit user request. A later request triggered a successful SDK build, flash, and UART reset; boot self-test passed, while two I2C transfer errors were logged during startup.

## Progress
- Check before: implementation and unified manual acceptance deferred by user; no tests, build, smoke check, or board operation run.
- Check after: updated seven project documents; workflow validation passed. Subsequent user request triggered SDK build and serial flash. UART `reboot` yielded `v0.4.2 boot ok`, `capsule selftest PASS (0)`, IMU ready, and UI ready; two I2C transfer errors were recovered during startup. W1–W5 feature acceptance remains pending.

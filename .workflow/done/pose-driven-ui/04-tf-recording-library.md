# 04: Multi-file TF recording library

**Effort:** pose-driven-ui
**Blocked by:** 02
**Files:** `project/proj.conf`, `project/Kconfig.proj`, `src/main.c`, `src/ui.c`, `capsule/src/`, `docs/06-风险与问题台账.md`
**Read first:** Spec `pose-driven-ui.md`; REQ-201; SDK v2.5.1 SPI MSD/FATFS examples and board SPI1 pinmux
**Check:** Deferred by user request. After all `pose-driven-ui` implementation tasks are complete, the user will perform unified manual acceptance; until then do not run tests, smoke checks, or builds.
**Covers:** W3
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Enable the board’s SPI1 TF driver and FAT filesystem using SDK configuration and existing pinmux.
- [x] Store each completed raw WAV as a unique file without overwriting existing recordings; handle mount, write, and full-card failures.
- [x] Implement file enumeration, WAV playback through the speaker, and confirmation before deletion.
- [x] Preserve the existing RAM recording path when the card is unavailable or a write fails, and report that it is not persistent.
- [x] Record the self-provided TF-card dependency and any unexecuted physical checks accurately.

## Progress
- Check before: deferred; user requested no development-stage tests, smoke tests, or builds. Unified manual acceptance will happen after all effort specifications are implemented.
- Check after: SPI1/TF/FATFS configuration, unique WAV persistence, RAM fallback, file listing/playback/deletion controls, and confirmation state implemented. No tests, build, smoke check, or board operation run per user instruction; unified manual acceptance remains pending.

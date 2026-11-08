# 10: Restore timer controls and completion beep

**Effort:** pose-driven-ui
**Blocked by:** pose-driven-ui/09
**Files:** `src/main.c`, timer/audio tests, evidence
**Read first:** Spec `pose-driven-ui.md` W1; `capsule/src/capsule.c`; SDK audio server usage
**Task type:** bugfix
**Covers:** W1
**Check:** `make -C capsule test`, SCons build, verify flash, and RTS reset have been run. The board booted with `capsule selftest PASS (0)`; timer completion, BEEP toggle, and SPK audibility still require physical UI interaction and hearing confirmation.
- [x] Configure 1–120 minute range with 1-minute step (`CAPSULE_TIMER_MIN_MINUTES 1u`, `CAPSULE_TIMER_MAX_MINUTES 120u`, `CAPSULE_TIMER_STEP_MINUTES 1u`).
- [x] Implement +/- tap and accelerating hold repeat in `ui_timer_adjust_event`: short click (`LV_EVENT_SHORT_CLICKED`) adjusts 1 minute, ignoring `LV_EVENT_CLICKED` to suppress extra release clicks; hold repeats with intervals 250 ms (<2s), 120 ms (>=2s), 60 ms (>=4s) starting after ~500 ms; clamps at 1 and 120 minutes.
- [x] Synchronize the BEEP UI snapshot and visible state with the timer state.
- [x] Advance running timer time even when an IMU sample fails; preserve pose pause/mute rules.
- [x] Refactor completion beep: 500 ms duration (amplitude 30000), volume 15 configured before `audio_open`, non-blocking worker thread (`timer_beep`) preserving main loop, backpressure retry on `written == 0` with offset tracking, flush query plus 50 ms tail margin; clear muted/disabled pending beep.
- [x] Verify board expiry trigger via temporary hardware probe: forced monotonic timestamps to expiry in mode 0 and mode 1 triggered actual `play_beep` on target, emitting UART `timer beep emitted`, DAC 16000/960, AW8155 PA start/mute 0/stop with zero audio errors. Probe removed; physical acoustic hearing confirmation remains open.
- [x] Final production source built with SDK v2.5.1, `sftool write_flash --verify` completed, RTS reset booted `capsule selftest PASS (0)`, IMU/FT6146/UI/BLE initialized, and 25-second UART capture had zero I2C errors/timeouts. Evidence: `evidence/2026-11-07-timer-adjust-beep.txt`.
- **Progress:** Host capsule regression `ok 283 checks`; extracted timer/audio smoke harness passed normal and failure branches. 1–120 min step 1 and accelerating +/- hold repeat implemented. Beep audio pipeline refactored to 500 ms, pre-open volume 15, non-blocking worker thread, backpressure retry, and 50 ms flush margin. Physical acoustic hearing and control hand-feel remain open.

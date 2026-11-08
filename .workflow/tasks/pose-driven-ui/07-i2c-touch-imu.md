# 07: Restore shared I2C touch and IMU operation

**Effort:** pose-driven-ui
**Blocked by:** 06
**Files:** `src/main.c`, `capsule/src/capsule.c`, relevant project docs and evidence
**Read first:** Spec `pose-driven-ui.md`; `.workflow/decisions.md` entry 2026-11-05; SDK `drv_touch.c`, `ft6146.c`, `i2c_core.c`; current UART evidence
**Task type:** bugfix
**Covers:** W1, W5
**Check:** Build and flash the real board; on UART confirm bounded I2C errors (no recurring recovery/timeouts during idle), stable pose without spurious transitions, and actual FT6146 pointer down/up reaches a UI command. Record before/after evidence. Do not mark touch acceptance complete unless a UI action visibly takes effect.
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e
**Status:** active
## Progress
 - Check before: 2026-11-06 UART evidence showed repeated `i2c_stat:20`/`i2c_errcode=24` and `i2c sem timeout` while app reassigned I2C1 PA39/PA40 each IMU sample. The official board page maps PA40/PA39 to sensor I2C1 and PA37/PA33 to touch I2C. The PA33/PA37 IMU change caused `read id failed`; actual touch actions were not observed.
 - Implemented: use official sensor pins PA40/PA39 for IMU initialization and raw reads, restore touch pins PA33/PA37 after each read, validate transfer success and acceleration magnitude, remove UI-time pinmux writes, and retain the rotated LVGL pointer path. Repositioned controls and increased touch targets.
 - Verification: `make -C capsule test` passed (163 checks); SCons build completed; `sftool write_flash --verify` returned exit code 0. Official RTS pulse reset produced `boot ok`, `capsule selftest PASS (0)`, `IMU ready`, FT6146 ID/open, and `UI ready`. A 35-second UART capture had 0 `i2c_stat` errors and 0 `i2c sem timeout` messages. Physical touch click and visual layout remain untested. Evidence: `evidence/2026-11-06-official-pinmap-boot.txt`.
 - Follow-up report from user: after the verified flash, the display was white with only `Text`. The cause in current code was that `ui_pose_label` had no text assigned, while all content controls were hidden unless a confirmed pose selected a page. The scroll page was also missing its 3×3 parent size/initial offset, so its 390×450 children could remain clipped outside the viewport.
 - Corrected by setting the 3×3 page size and centering offset, assigning/updating a visible pose label, and connecting FACE_DOWN to the LCD backlight brightness control with restore on the next known pose. Rebuilt, flashed with verify, and RTS reset; boot log confirmed application, IMU, FT6146, and UI initialization, with 0 I2C errors/timeouts across the captured 22 seconds. No camera view of the development board or physical touchscreen tap has been captured; visual content and click response are still awaiting confirmation from the board screen.
- Follow-up user-reported control failure: inspection found `main()` performed only one iteration; the `while (1)` loop was missing after an earlier timer edit. Restored the permanent main loop, added shared recursive I2C locking across pinmux/transfer/restore, queue-overflow diagnostics, command-dispatch logs, and removed silent pose guards on visible recorder actions. Final build, verify flash, and RTS reset completed; 30-second UART capture showed `capsule selftest PASS (0)`, IMU/FT6146/UI initialization, and zero I2C errors/timeouts. Physical touch click and visual layout remain untested. Evidence: `evidence/2026-11-06-lcpu-ble-boot.txt` and `evidence/2026-11-06-official-pinmap-boot.txt`.
- [x] Use official PA40/PA39 sensor and PA33/PA37 touch mappings; restore touch mapping after each IMU transaction and keep UI code free of pinmux writes.
- [x] Ensure failed/invalid accelerometer reads cannot be passed as pose samples or trigger pose/timer/UI transitions; use a reliable read-success boundary.
- [x] Verify LVGL touch points are transformed into the rotated logical display coordinates, and touch down/up status is preserved.
- [ ] Exercise actual touch controls and verify a visible timer action plus serial event evidence.
- [x] Update the risk ledger and verification evidence with observed I2C behavior; retain remaining risks honestly.

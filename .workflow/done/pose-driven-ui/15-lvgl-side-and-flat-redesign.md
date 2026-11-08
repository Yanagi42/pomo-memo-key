# 15: LVGL side media remote and flat voice memo redesign

**Effort:** pose-driven-ui
**Blocked by:** pose-driven-ui/14
**Files:** `src/main.c`, `src/tf_library.c`
**Read first:** `.workflow/specs/pose-driven-ui.md` (W2, W3, W4, W5); `design.html` (`#side-view-container` and `#flat-view-container`)
**Task type:** feature
**Covers:** W2, W3, W4, W5
**Check:** `make -C capsule test` passes and SCons firmware build (`project/`) succeeds with all four pose screens wired to the global light/dark theme.
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Redesign both `CAPSULE_POSE_SIDE_LEFT` and `CAPSULE_POSE_SIDE_RIGHT` panels in `src/main.c` to match `design.html`: top bar with `DESK·02` label and `CONNECTED` / `OFFLINE` Bluetooth status pill, 3×2 ceramic icon button grid (`VOL -`, `VOL +`, `PREV`, `NEXT`, `MUTE`, primary-accent `PLAY/PAUSE`), disabled visual state when BLE is disconnected, and bottom transient feedback bar (`VOL + TRIGGERED`, `MUTED`, etc. reverting after 1.5 s).
- [x] Redesign `CAPSULE_POSE_FLAT` voice memo panel in `src/main.c` to match `design.html`: recessed card with top filename/status label (`MEMO_XXXX.WAV`, `RECORDING...`, `MEMO_RAM.WAV`, or error), top-right 7-bar capsule waveform indicator (static low at idle, animated during recording/playback, danger-red while recording), middle `00:SS` elapsed/duration and `00:30` limit labels, and bottom read-only capsule progress bar with knob.
- [x] Implement the 4-button bottom row on the flat panel: `[Folder / FILE cycle]` (primary-accent when TF is ready, standard ceramic in RAM mode), `[Play / Stop]`, `[Record / Stop]` (dual-ring icon, danger-red while recording), and `[Delete / CONFIRM?]` (in-place danger-red confirmation, auto-reset on file switch, new recording, or leaving flat pose).
- [x] Allow tapping `[Play / Stop]` while playback is active to stop `tf_library` playback cleanly, and keep KEY2 global flash-recording / side-macro semantics intact.
- [x] Ensure `ui_theme_apply(bool dark)` updates all widgets across stand, left/right remote, and flat memo panels without leftover hardcoded light surfaces.

## Progress

- **Check after:** Exact `make -C capsule test` passed with 389 checks, exit code 0 (`ok 389 checks`). Exact SCons firmware build (`bash -c 'source /home/shizuku/.sifli/SiFli-SDK/main/export.sh -t gcc && cd project && scons --board=sf32lb52-lchspi-ulp -j8'`) succeeded with exit code 0, binary size: `main.bin` 896,600 bytes, ROM used: 947,587 B, RAM used: 205,244 B; preserved build log: `/tmp/scons_final.log` and `/tmp/pose-ui15-evidence/scons_final.log` (and synchronized `/tmp/scons_build.log`). Real SDK LVGL 8 host smoke test (`/tmp/pose-ui15-evidence/smoke.log`) passed all 7 checks (exit 0) verifying 450×390 left/right remote geometry, bullet symbol (`0x2022`) in Montserrat 14/16 via `lv_font_get_glyph_dsc`, flat memo recessed card, wave capsule, read-only progress bar, 4-button action bar, BLE disconnect guards, pointer indev coordinate translation to button centers, command queue dispatch, 1500 ms feedback expiry, KEY2 short/long press side macros, flat record/delete/stop invocation, dark palette theme switch, empty-ready vs unmounted RAM memo status, failed TF save RAM fallback, guarded unseen TF deletion, FILE cycle with selected TF playback and no stale TRUNCATED, and no clobbered RAM playback. Audio cancellation harness (`/tmp/pose-ui15-evidence/tf_cancel_result.log`) passed all 10 scenarios with ASan/UBSan. Hardware acceptance on board/ttyUSB0 deferred to task 13.
- **Review Status:** Review PASS base `7371246a783a581d42588b1dae694c4d8fa1be5e` (all 4 findings resolved: bullet separator in Montserrat, explicit RAM memo source & validity tracking, non-forcing TRUNCATED on FILE cycle, guarded DELETE of unseen TF files; stale media toggle item withdrawn).

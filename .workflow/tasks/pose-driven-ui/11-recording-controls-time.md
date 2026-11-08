# 11: Restore recording controls and elapsed-time display

**Effort:** pose-driven-ui
**Blocked by:** pose-driven-ui/10
**Files:** `src/main.c`, `src/tf_library.c`, tests, evidence
**Read first:** Spec `pose-driven-ui.md` W3; recording callback and RAM fallback path; task 08 UI layout
**Task type:** bugfix
**Covers:** W3
**Check:** `make -C capsule test`, SCons build, verify flash, and RTS reset have been run. The board booted with `TF library RAM fallback`; RECORD/FILE/PLAY/DELETE interaction, elapsed display, microphone capture, and SPK playback still require physical UI/audio interaction.
- [x] Process RECORD/FILE/PLAY/DELETE taps consistently with the visible recorder UI, with explicit feedback for rejected or unavailable actions.
- [x] Show elapsed recording duration while recording and retain the completed recording's duration after stop.
- [x] Ensure FILE selection status changes for a mounted library and gives a visible no-card/no-selection state otherwise.
- [x] Keep RAM recording available without a TF card and report persistence accurately.
- [x] Record observed board logs and remaining TF-card/audio requirements honestly.
- **Progress:** Host capsule regression passed 163 checks. Final firmware built and flashed; no physical recording workflow was executed.
- Follow-up review found `audio_open` failure had no visible recorder feedback. Added `TF_UI_RECORDING_FAILED` with a microphone-unavailable UI message; final build, verify flash, and RTS boot completed. Physical recording workflow remains unexecuted.
- Playback progress & concurrency: Added recorder status label (`PLAYBACK READY`, `PLAYING N%`, `PLAYBACK COMPLETE`, `PLAYBACK FAILED`) and a 0–100% LVGL progress bar. PLAY dispatch moves playback to a dedicated worker thread (`play_memo`), so playback is asynchronous and the main loop and LVGL snapshot updates continue during audio output. The label text and bar value are only written when the displayed state or percentage changes. Progress increments as audio chunks are accepted by the audio server and reaches 100% after flush completion. While playback owns the shared WAV buffer and TF selection, duplicate PLAY is ignored, starting a new recording is rejected to prevent overwrite, and FILE change and DELETE are rejected. Built with SiFli SDK v2.5.1; host capsule tests passed 163 checks; flashed via `sftool write_flash --verify` (exit code 0); RTS reset boot UART confirmed `capsule selftest PASS (0)`, IMU/FT6146/UI ready, BLE power event and advertising start, and a 28-second capture had zero I2C errors and timeouts (`evidence/2026-11-07-playback-progress.txt`). Physical PLAY tap, visible progress changes during playback, and speaker playback audibility remain unverified.
- Playback mutual exclusion refinement: PLAY is rejected if recording is active, preventing playback of a partial recording while capture/stop mutates the shared WAV storage. Final code after this guard built, verify-flashed, and RTS-booted successfully; physical PLAY/UI/audio acceptance remains open.

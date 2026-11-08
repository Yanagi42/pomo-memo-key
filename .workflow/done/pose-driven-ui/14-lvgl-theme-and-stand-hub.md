# 14: LVGL theme foundation, neutral containers, and stand three-mode hub

**Effort:** pose-driven-ui
**Blocked by:** None
**Files:** `capsule/src/capsule.h`, `capsule/src/capsule.c`, `capsule/test/test_capsule.c`, `capsule/Makefile` (only the test-source path), `project/proj.conf`, `src/main.c`
**Read first:** `.workflow/specs/pose-driven-ui.md` (W1, W2); `.workflow/decisions.md` (2026-11-07 entries); `design.html` (`#stand-view-container` and CSS tokens)
**Task type:** feature
**Covers:** W1, W2
**Check:** `make -C capsule test` passes and SCons firmware build (`project/`) succeeds with `CONFIG_LV_FONT_MONTSERRAT_48=y`.
- **Base commit:** 7371246a783a581d42588b1dae694c4d8fa1be5e

- [x] Extend `capsule` timer state machine with manual start/pause toggle, reset-to-preset, and a stand-submode gate so entering `CAPSULE_POSE_STAND` only auto-starts/resumes Pomodoro when the active stand submode is `POMO` (or a timer was manually started), while leaving `CAPSULE_POSE_STAND` always auto-pauses; add unit tests in `capsule/test/test_capsule.c`.
- [x] Enable `CONFIG_LV_FONT_MONTSERRAT_48=y` in `project/proj.conf`.
- [x] In `src/main.c`, remove the top-level `WAITING FOR POSE` debug label and zero out `border_width`, `radius`, `outline_width`, `pad_all`, and scrollbars on the active screen, `ui_page`, and all four pose panels so the 450×390 canvas has no residual LVGL default theme borders or offsets.
- [x] Implement the global light/dark ceramic theme token table (`ui_theme_apply(bool dark)`) covering screen background, recessed display card, standard button, primary accent button, danger button, pill/chip borders, and three text hierarchy levels.
- [x] Build the `CAPSULE_POSE_STAND` three-mode hub (`POMO` / `TIME` / `STOPWATCH`) with top segmented pill bar, horizontal swipe gesture switching (>40 px threshold without stealing button taps), default boot into `TIME`, and RAM-memorized submode across pose transitions.
- [x] Implement the three stand subpanes matching `design.html`: `POMO` (recessed card with centered countdown, right-anchored BEEP bell button, status dot/label, and `[- / ↺ / ▶⏸ / +]` 4-button row with 1-min tap and accelerating hold), `TIME` (on-chip RTC `time`/`localtime`, centered `HH:MM`, baseline-hanging `:SS`, superscript `AM/PM`, date row, and `[12H/24H / Seconds / Night Mode]` 3-button row), and `STOPWATCH` (100 ms `MM:SS.d` counter, `NO LAP` / `LAP N · MM:SS.d` pill strip, and `[↺ / ▶⏸ / ⚑]` 3-button row).

## Progress

- **Check after:** Exact `make -C capsule test` passes: `ok 389 checks`, exit 0 (ASan+UBSan, no leaks). Exact SCons command on final source: exit 0, `main.bin` 893,888 B, 0 errors.
- **Path-mismatch resolution:** `capsule/Makefile` hardcoded `tests/test_capsule.c` (the pre-existing user worktree test) while the task specified `capsule/test/test_capsule.c`; that mismatch, not an immutable blocker, caused the earlier 52/283 failures. The Makefile now builds `test/test_capsule.c` with the same `-std=c11 -Wall -Wextra -Werror -Isrc -fsanitize=address,undefined` flags and `src/capsule.h` prerequisite; `src/vad.c` was already removed from the file list by the user. `capsule/tests/test_capsule.c` is untouched (mtime 2026-11-07, deliberate user work).
- **Suite integration:** All 15 original `test_*` scenarios and `capsule_selftest` moved verbatim into the canonical file under one `main`, with `CHECK` counters preserved (389 checks). Their timer/pose scenarios now explicitly `capsule_set_stand_mode(CAPSULE_STAND_POMO, 0)` before expecting pose auto-run/resume; no production semantics, assertions, preprocessor inclusion, or legacy mode were changed. Default `TIME` safety and the two new stand-hub tests (`test_stand_hub_pose_gate`, `test_stand_hub_expiry_zero`) run deterministically on per-test fixtures.
- **Host smoke (real LVGL 8, throwaway harness, removed):** `ok stand hub host smoke`, exit 0, `input-path failures=0`; details recorded below.
- **Findings fixed (review):** Expiry branch restores `*active_remaining(box) = 0` (visible `00:00`, restart reloads preset); `ui_theme_apply(bool dark)` owns RAM `ui_night_mode` and toggles only request a flag consumed on the LVGL thread; `:SS` baseline offset is `montserrat_24.base_line - montserrat_48.base_line` (−4) with layout update before the `AM/PM` width read; `ui_tf_next_button` binds `ui_primary_style` under `CHECKED`; duplicated `ui_set_active(record/delete/file)` trio removed.
- **Design tokens:** Rejected as defect — user's exact color values override `design.html`, so the current token table is intentional.
- **Runtime evidence (real LVGL pointer indev):** `tab tap -> mode=1 checked=[0,1,0]`; `short drag mode=1`; `vertical drag mode=1`; `swipe left mode=2`; `swipe right mode=1`; `POMO tap start state=1 … started=7200 after3s=7197`; `POMO tap pause state=2 frozen=7197 after3s=7197`; `POMO tap reset state=0 seconds=7200 text=120:00`; `STOPWATCH after lap text=00:12.8 lap0=LAP 1 • 00:12.6`; `STOPWATCH paused text=00:12.9 after2s=00:12.9`; `STOPWATCH reset text=00:00.0 lap0=NO LAP`. Geometry: `HH:MM`/`:SS` baseline delta 0 (old +8), 12H `AM/PM` clears seconds, 120:00 width 152 px centered with 73 px gap to the right-anchored bell, POMO expiry `00:00` + `COMPLETED`, theme `#f7fafc` ↔ `#0b111e` persists across frames.
- **Base:** 7371246a783a581d42588b1dae694c4d8fa1be5e (unchanged; no commits required).
- **Review pass:** Independent canonical-test review PASS with no findings — all 15 original `test_*` functions preserved with only explicit POMO setup added, `capsule/tests/test_capsule.c` untouched, `capsule/Makefile:13-14` builds the canonical path, single `main` calls 15 old + 2 stand-hub tests, default `TIME`/gate assertions intact. Director verified the same lines and Progress evidence.
- **Blockers:** None.

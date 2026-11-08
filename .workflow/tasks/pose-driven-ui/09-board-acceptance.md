# 09: Verify touchscreen command and page baseline

**Effort:** pose-driven-ui
**Blocked by:** pose-driven-ui/08
**Files:** `src/main.c`, `evidence/`
**Read first:** Spec `pose-driven-ui.md` W2/W5; tasks 07 and 08; SDK LVGL rotation and FT6146 input paths
**Task type:** bugfix
**Covers:** W2, W5
**Check:** Build and flash the current image, observe UART, then tap one visible control on each of timer, left/right remote, and recorder screens; each tap must reach application dispatch, and no recurring I2C errors/timeouts may occur.
- [ ] Confirm each visible page control reaches the application command handler; expose a useful state/error instead of silently dropping a rejected action.
- [ ] Record real UART evidence for touch dispatch and idle/active I2C behavior.
- [ ] Keep audio, recording, Bluetooth command behavior, and final W1–W5 regression in tasks 10–13.

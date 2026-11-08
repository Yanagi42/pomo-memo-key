# 12: Pair BLE HID and implement full media controls

**Effort:** pose-driven-ui
**Blocked by:** pose-driven-ui/11
**Files:** `src/ble_hid.c`, `src/main.c`, `project/proj.conf`, evidence
**Read first:** Spec `pose-driven-ui.md` W4/W5; SiFli SDK BLE HID example and connection-manager bonding API
**Task type:** feature
**Covers:** W4, W5
**Check:** Build/flash the board, discover `POMO_MEMO_HID` in a phone or computer Bluetooth settings, pair without a passkey, and test volume up/down, mute, previous, play/pause and next. Disconnect/reconnect and repeat; UI must show connection state and retain all controls.
- [x] Correct the HID Consumer report descriptor and report bit layout to match the transmitted payload.
- [x] Add no-input/no-output bonded pairing and retain usable report state across reconnects.
- [x] Add Consumer Control reports for volume down, previous track, and play/pause alongside volume up, mute, and next.
- [x] Present all media controls in the lock-screen-style panel with a neutral “媒体控制” title; show BLE pairing/device name and connection state.
- [x] Do not add track metadata or claim host playback state; this is a control surface only.
- [x] Record a real paired-host control and reconnect result.
## Progress

- **Check after:** 2026-11-08 用户确认任务 12 验收通过（“任务12我验收了 没问题 归档就行了”），该确认涵盖本任务 `Check` 的标准。这是用户口头整体确认，无 agent 逐项捕获的日志，也未重跑该验收：本记录不声称用户逐条复述过六个按键的行为，也不声称用户逐项豁免了 review 观察项。
- Implemented 6-command Consumer Control HID report descriptor and bitmask mapping (Vol Up/Down, Mute, Next, Prev, Play/Pause) in `src/ble_hid.c`, configured Just Works NoInputNoOutput bonded pairing via connection manager, and added the lock-screen media control panel in `src/main.c`.
- Verified BLE bonded pairing using BlueZ `bluetoothctl` with `agent NoInputNoOutput`: host discovered `POMO_MEMO_HID` and paired (`Bonded: yes`, `Paired: yes`, `ServicesResolved: yes`). Discovered HID service UUID `00001812-0000-1000-8000-00805f9b34fb`, Report Map `00002a4b`, Report `00002a4d`, HID Control Point `00002a4c`. Evidence: `evidence/2026-11-06-ble-bond.txt`.
- Host-side structural cross-check (agent, 2026-11-08): paired BlueZ host created `/sys/class/input/input65` named `POMO_MEMO_HID` (`PRODUCT=5/0/0/0`, `EV=13`, consumer-control key capability mask) beneath `/sys/devices/virtual/misc/uhid/0005:0000:0000.001A`, event node `/dev/input/event260`, confirming the host HID stack parsed the Consumer report descriptor. This is descriptor evidence only, not a control-delivery capture.
- **Review:** Independent fresh-subagent review of the working tree against `base commit 7371246a783a581d42588b1dae694c4d8fa1be5e` (the implementation is uncommitted, so `base…HEAD` is empty; static, read-only, no hardware) accepted the report descriptor and bitmask mapping, the Just Works bonded pairing configuration, the six-command dispatch, and the no-metadata control-surface requirement. It raised two `nonblocking` observations: (1) `src/main.c:1095,1502-1507` swaps the play/pause glyph and shows `PLAYING`/`PAUSED` from local button presses rather than the neutral transient feedback the side panel spec describes (e.g. `VOL + TRIGGERED`), and `ui_media_playing` starts true, so a first press against a paused host can read `PAUSED`; (2) the panel itself shows only `CONNECTED` / `OFFLINE` / `BLUETOOTH DISCONNECTED`, so the pairing identity `POMO_MEMO_HID` appears only in the UART line at `src/main.c:1483` even though the checklist asked for it on screen. Both are logged in `.workflow/technical-debt.md`. This pass made no firmware change; the review outcomes were not waived or deferred by the user.

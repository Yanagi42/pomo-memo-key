# Decisions

Append-only log of hard-to-reverse choices where a real alternative was rejected
(CONVENTIONS §1). Skip routine implementation details. Git union-merges concurrent
additions.

## Format

Add `## <YYYY-MM-DD> - <title>` entries under `## Entries` (newest last). Cite as
`decisions.md: <date> - <title>`. Every entry requires three non-empty English field names
(values may be in any language):

- **Decided:** what was chosen
- **Instead of:** the strongest alternative that lost
- **Because:** why it lost here

Optional: **Decided by** (`user` or `agent`), **Revisit when**
(condition to reconsider), and **Supersedes** (`<YYYY-MM-DD> - <old title>`;
leave the older entry untouched so the file stays strictly append-only).

```markdown
## 2026-01-31 - Retries capped at 3, no backoff

- **Decided:** three immediate retries, then fail.
- **Instead of:** exponential backoff with jitter.
- **Because:** the only caller retries within a single user request; a 30-second
  backoff would outlive the request it is serving.
- **Decided by:** user
- **Revisit when:** a background job starts calling this.
```

## Entries

<!-- Add yours below. Delete this comment. -->
## 2026-11-05 - Defer LVGL until the shared I2C1 bus is resolved

- **Decided:** keep the proven serial/IMU/timer/recording P0 firmware and defer LVGL UI integration.
- **Instead of:** shipping the first LVGL label task immediately.
- **Because:** `littlevgl2rtt_init("lcd")` reconfigured the board's shared I2C1 path; the real boot run then produced corrupted IMU values and I2C errors. Shipping that build would regress the verified product path.
- **Decided by:** agent
- **Revisit when:** the display/touch and LSM6DS3TR-C bus ownership/pinmux are separated or serialized.
## 2026-11-06 - Use bonded Just Works for BLE media controls

- **Decided:** pair and bond `POMO_MEMO_HID` with no input/output and no passkey; show connection/pairing guidance in the remote UI and implement media button reports only, without player metadata.
- **Instead of:** a displayed/entered PIN and Apple-specific or platform-specific media metadata services.
- **Because:** the user selected no-passkey pairing and requested control buttons only; metadata availability is not portable across the target hosts.

## 2026-11-07 - Stand-pose three-mode hub defaults to TIME with RAM-memorized submode

- **Decided:** The stand-pose screen hosts three swipeable/tappable submodes (`POMO` / `TIME` / `STOPWATCH`), defaults to `TIME` on boot, remembers the selected submode in RAM across pose changes, and only auto-starts/resumes the Pomodoro countdown on entering `CAPSULE_POSE_STAND` when the active submode is `POMO` (or a timer was manually started), while always auto-pausing on leaving `CAPSULE_POSE_STAND`.
- **Instead of:** Unconditionally auto-starting Pomodoro whenever the device stands upright, or making Pomodoro purely manual without pose auto-pause.
- **Because:** Defaulting to the desktop clock (`TIME`) while unconditionally running Pomodoro in the background would cause unexpected expiry beeps while the user is only using the clock; gating auto-run on the `POMO` submode preserves both the clock-first experience and the posture-driven Pomodoro workflow.
- **Decided by:** user

## 2026-11-07 - Native LVGL 8 ceramic theme, built-in Montserrat/symbols, and RTC clock source

- **Decided:** Implement the 450×390 viewport UI from `design.html` using native LVGL 8 widgets, neutralized container borders/radii, `lv_font_montserrat_48` for primary digits, built-in LVGL symbols plus geometric primitives for icons/waveforms, global light/dark theme tokens, and the on-chip RTC (`time`/`localtime` + MSH `date`) for the `TIME` submode, excluding all external HTML simulator chrome.
- **Instead of:** Embedding bitmapped SVG/custom font assets, adding an on-screen clock-setting modal, or adding WAV byte-offset seek to `tf_library`.
- **Because:** Native LVGL objects and built-in symbols switch cleanly between light and dark themes with zero flash asset overhead, and 30-second voice memos do not need seek.
- **Decided by:** user

## 2026-11-08 - Archive task 12 on the user's stated acceptance

- **Decided:** Archive `pose-driven-ui/12` on the user's 2026-11-08 statement that task 12 was accepted and should be archived ("任务12我验收了 没问题 归档就行了"), recording that confirmation as covering the task's `Check` without any agent-run per-item log.
- **Instead of:** Re-running the paired-host and physical acceptance as an agent before archiving.
- **Because:** The user owns real-board acceptance and reported it passed; re-running would duplicate their check and delay integrated acceptance.
- **Decided by:** user

## 2026-11-08 - Keep task 12 review observations as debt without firmware change

- **Decided:** Record the two `nonblocking` observations from the independent task-12 review (locally inferred `PLAYING`/`PAUSED` glyph and label; pairing identity shown only in UART) in `.workflow/technical-debt.md` and leave the firmware unchanged in this acceptance pass.
- **Instead of:** Fixing them in `src/main.c` before archiving the task.
- **Because:** They are cosmetic and do not affect the media-control path the user accepted; task 15 already specifies `PLAYING`/`PAUSED` transient feedback, so the label is intended behavior and only the initial glyph direction is ambiguous.
- **Decided by:** agent

## 2026-11-12 - Board-side BLE file transfer uses a self-built GATT service

- **Decided:** Implement REQ-203 on the board with a private 128-bit GATT service in `src/ble_file.c` (`c0de0001-…`, `cmd` write characteristic, `data` notify characteristic), mirroring the proven `ble_hid.c` registration/event pattern, and keep the line protocol from `docs/11` unchanged.
- **Instead of:** Reusing the SDK serial-transmission service via `rt_ble_serial_dev_register()`, or restarting serial-transmission with BLE console enabled.
- **Because:** `rt_ble_serial_dev_register` lives in `bf0_sibles_console.c`; enabling it moves the console onto BLE and takes away the UART used for debugging and acceptance evidence on COM5. A private service also avoids ATT-layer reassembly and leaves HID advertisement untouched.
- **Decided by:** user (rejection of the serial-transmission path) and agent (implementation shape).



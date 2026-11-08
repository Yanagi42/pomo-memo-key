# 2026-11-08 task 13 — evidence summary

| Item | Exact command / action | Exit | Captured | Observed |
|---|---|---|---|---|
| Build | `source /home/shizuku/.sifli/SiFli-SDK/main/export.sh -t gcc && cd project && scons --board=sf32lb52-lchspi-ulp -j8` | 0 | `build.log` (6,913 B) | `Activating SiFli-SDK v2.5.1`; `scons: done building targets.`; `Binary size: 896,600 B`; HCPU ROM used 947,587 B, RAM 205,244 B; LCPU `main.lcpu` 6,432 B; no compiler error/warning diagnostics (only the SDK export notice `worktree is dirty; environment export will continue because the compatibility hash is unchanged` at `build.log:9`). Incremental — no app recompile; `main.bin` (13:27:39) newer than every input (`src/main.c` 13:26:57, `capsule/src/capsule.c` 00:48:45, `src/ble_hid.c` 2026-11-06 20:19). |
| Flash | `sftool --connect-attempts 1 -p /dev/ttyUSB0 -c SF32LB52 -m nor write_flash --verify "bootloader/bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab/ftab.bin@0x12000000"` | 0 | `flash.log` | All write steps reached `100%`; md5 `main.bin` c54c90b2…, `bootloader.bin` 3426e1b1…, `ftab.bin` 8a031b47…. Port free before use (`fuser -v /dev/ttyUSB0` exit 1); no user process killed. |
| Reset + boot A | RTS high 0.5 s → low, then read 1,000,000 baud | 0 | `boot-rts.txt`, 25 s, 3,381 B | `SFBL`; `2.5.1 build 421126d9`; `v0.4.2 boot ok`; `capsule selftest PASS (0)`; `IMU ready: bus=i2c1 addr=0x6a sample=100ms`; `TF library RAM fallback`; `[SD] no card (pin 27)`; FT6146 opened; `UI ready`; `set brightness 50`; `display on`; `advertising start result=0 (name=POMO_MEMO_HID)`; pose `未知 → 直立`. |
| Reset + boot B | RTS high 0.5 s → low, then read 1,000,000 baud | 0 | `boot-rts-2.txt`, 18 s, 3,385 B | Same boot/self-test/UI/BLE lines; pose `未知 → 平放`, showing the IMU path follows real orientation. |
| I2C health | grep both boot captures | — | — | 0 × `i2c_stat`, 0 × `i2c_errcode`, 0 × `i2c sem timeout`; no `WAITING FOR POSE`, no `read id failed`. |
| Regression | `make -C capsule test` | 0 | `capsule-test.log` | `ok 389 checks` (single final run, ASan+UBSan, no leak). |
| Host bond | `bluetoothctl info 5C:FD:52:80:07:88` | 0 | (in this file) | `Paired: yes`, `Bonded: yes`, `Connected: yes`, HID UUID `00001812-…`. Host HID node `input65` / `NAME="POMO_MEMO_HID"` / consumer key mask under `/sys/devices/virtual/misc/uhid/0005:0000:0000.001A`; recreated as `input72` after reconnect. Descriptor-parse evidence only. |
| Disconnect + reconnect | `bluetoothctl --agent NoInputNoOutput disconnect` (13:47:43), then `connect` (13:47:50) | 0 | `reconnect-uart.txt`, 35 s, 3,596 B | Host: `Disconnection successful` → `Connection successful`, `Connected: yes`, bond retained (no re-pair). Firmware: `disconnected conn=0 reason=0x13` → `ADV started status=0` → `connected conn=0 peer=28:d0:43:82:eb:ed` → `link encrypted`; 0 I2C errors. (One disconnect→reconnect cycle captured.) |

## Boundaries — not exercised, no result claimed

Physical, not automatable from this session, still pending:

- Touchscreen taps and visible UI: W1 stand hub (POMO/TIME/STOPWATCH), W2 page transitions/theme, W3 flat memo page, W5 side panel rendering, disabled-button look, transient feedback bar.
- Timer completion `BEEP` audibility; long-press accelerate feel.
- Microphone capture, elapsed-duration display, SPK playback.
- TF-card mount / FILE cycle / playback progress / two-step `CONFIRM?` delete — `[SD] no card` observed, no card inserted.
- Media-control delivery to a host (W4): not agent-run here. Task 12 is archived on the user's 2026-11-08 blanket statement that the task passed, which covers its own `Check`; there is no agent per-item log of the six keys, and it was deliberately not re-run.

## Notes

- First reconnect attempt produced an empty capture because pyserial asserts `RTS` on open, holding this board in reset. All reported captures deassert `RTS`/`DTR` right after opening; the empty file was overwritten by the corrected run.
- `reconnect-uart.txt` opens with a full boot banner: deasserting `RTS` at open releases the reset the previous capture had imposed, so the board booted again. The captured `disconnected reason=0x13 → ADV started → connected → link encrypted` sequence is the reconnect cycle, not a fresh boot artifact.
- The throwaway capture helper lives at `/tmp/task13-serial-capture.py`, outside the repository.

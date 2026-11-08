# 2026-11-08 task 13 integrated acceptance — agent-run evidence

Board: Huangshan Pai `sf32lb52-lchspi-ulp` (SF32LB52), serial `/dev/ttyUSB0` (CH340, `1a86:7523`), 1,000,000 8N1.
SDK: SiFli-SDK v2.5.1 (`Activating SiFli-SDK v2.5.1`, board firmware banner `build on Nov  8 2026, 2.5.1 build 421126d9`).
Working tree: uncommitted on `main`, `Base commit` 7371246a783a581d42588b1dae694c4d8fa1be5e. No source, config, or SDK changes were made in this pass.

## 1. Build — exit 0

```sh
source /home/shizuku/.sifli/SiFli-SDK/main/export.sh -t gcc && cd project && scons --board=sf32lb52-lchspi-ulp -j8
```

- exit code `0`; full transcript `build.log`.
- Result: `Binary size: 896,600 B`; HCPU ROM used 947,587 B, RAM used 205,244 B; LCPU `main.lcpu` 6,432 B; `scons: done building targets.` No compiler error or warning diagnostics in the log; the only `[WARN]` line is the SDK export notice `worktree is dirty; environment export will continue because the compatibility hash is unchanged` (`build.log:9`).
- Incremental run: SCons reported no application recompiles because every target was already current. Currentness was checked instead of forced: `find src capsule/src project/proj.conf project/SConstruct project/SConscript project/Kconfig.proj -newer project/build_sf32lb52-lchspi-ulp_hcpu/main.bin` returned empty, i.e. `main.bin` (2026-11-08 13:27:39) is newer than every input (`src/main.c` 13:26:57, `capsule/src/capsule.c` 00:48:45, `src/ble_hid.c` 2026-11-06 20:19).
- Artifacts flashed: `main.bin` md5 `c54c90b2c7e22c5e5d5e2a40b0c1172f`, `bootloader/bootloader.bin` md5 `3426e1b15ec195b165c3ad7b50cbcf06`, `ftab/ftab.bin` md5 `8a031b479ea53845567ff7c3bd6b9510`.

## 2. Verify flash — exit 0

```sh
sftool --connect-attempts 1 -p /dev/ttyUSB0 -c SF32LB52 -m nor write_flash --verify \
  "bootloader/bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab/ftab.bin@0x12000000"
```

- run from `project/build_sf32lb52-lchspi-ulp_hcpu/` after the SDK export; exit code `0`; transcript `flash.log` shows all progress steps reaching `100%`.
- `/dev/ttyUSB0` was free: `fuser -v /dev/ttyUSB0` → exit 1 (no holder). No user process was killed; the port was opened read/write only for the runs below.

## 3. RTS reset and startup capture — two clean boots

Reset method: `RTS` held high 0.5 s then low, then capture `boot-rts.txt` (25 s) and later `boot-rts-2.txt` (18 s).

Observed on both runs (exact lines from `boot-rts.txt` / `boot-rts-2.txt`):

```
SFBL
Serial:c2,Chip:4,Package:3,Rev:3  Reason:00000000
- SiFli Corporation
 / | \     build on Nov  8 2026, 2.5.1 build 421126d9
aw32001_init ok,  time consum: 0.00 ms.
mount /dev sucess
[BUS]spi1 probe sdcard...
[SD] no card (pin 27), skip msd_init
[I/TOUCH] Regist touch screen driver, probe=12028a95
[course_starter] v0.4.2 boot ok
[course_starter] capsule selftest PASS (0)
[pomo_hid] ble_hid_init started
[pomo_hid] calling sifli_ble_enable
[I/sensor.st.lsm6dsl] sensor init success
[course_starter] IMU ready: bus=i2c1 addr=0x6a sample=100ms
[course_starter] TF library RAM fallback (initial source=RAM)
[course_starter] using saved five-pose calibration
[I/co5300] LCD module use CO5300 IC
[I/TOUCH] touch screen found driver  20028a74, ft6146
[I/TOUCH] Opened.
[course_starter] RTC initialized to 2026-11-08 10:42:00
[course_starter] UI ready: interactive timer, touch enabled by SDK input driver
[I/DBG] set brightness 50
[I/DBG] display on
[pomo_hid] BLE power ready; configure Just Works bonding
[pomo_hid] service_init handle=2002d1ec
[pomo_hid] advertising start result=0 (name=POMO_MEMO_HID)
[pomo_hid] ADV started status=0 mode=0
```

Absent from both captures: `i2c_stat`, `i2c_errcode`, `i2c sem timeout`, `WAITING FOR POSE`, `read id failed` — grep count `0` for all three I2C error patterns. This only shows that the startup log completes and the application keeps running past its loop entry: no white-screen or blank-display symptom can be read from UART, and the visible screen was not observed in this pass (see §6).

Pose reporting observed on UART and differed between boots, i.e. the IMU path is live and the frame reported follows the physical orientation of the board on the desk:

- `boot-rts.txt`: `status=未知 25:00 x=0.204 y=-0.977 z=0.015` → `status=直立 25:00 x=0.207 y=-0.980 z=0.012`
- `boot-rts-2.txt` (lines 96-97): `status=未知 25:00 x=-0.059 y=-0.054 z=-0.979` → `status=平放 25:00 x=-0.055 y=-0.056 z=-0.981`

`[SD] no card (pin 27), skip msd_init` plus `TF library RAM fallback` is the observed TF state: no card present, RAM fallback taken. FAT/`msd`-card paths therefore could not be exercised.

## 4. Regression — one final run, exit 0

```sh
make -C capsule test
```

- exit code `0`; transcript `capsule-test.log`; output `ok 389 checks` (ASan+UBSan build, no leak report).

## 5. BLE pairing and disconnect/reconnect — host-side run

Host: BlueZ `bluetoothctl` 5.87, controller `28:D0:43:82:EB:ED`, `--agent NoInputNoOutput`.

- Pre-existing bond still valid: `bluetoothctl info 5C:FD:52:80:07:88` → `Paired: yes`, `Bonded: yes`, `Connected: yes`, `UUID: Human Interface Device (00001812-0000-1000-8000-00805f9b34fb)`.
- Host HID stack accepted the Consumer report descriptor: `/sys/class/input/input65` (`NAME="POMO_MEMO_HID"`, `PRODUCT=5/0/0/0`, `EV=13`, consumer key mask) under `/sys/devices/virtual/misc/uhid/0005:0000:0000.001A`; after the reconnect the node was recreated as `input72`. This shows descriptor/host-parse success only, not a delivered media command.
- Disconnect at 13:47:43 → `Connected: no`, `Disconnection successful`. Connect at 13:47:50 → `Connected: yes`, `Connection successful`; `Paired: yes`, `Bonded: yes` retained afterwards with no re-pair.
- Firmware UART during the same window (`reconnect-uart.txt`, 35 s):

```
[pomo_hid] connected conn=0 peer=28:d0:43:82:eb:ed intv=39
[pomo_hid] ADV stopped reason=0 mode=0
[pomo_hid] link encrypted
[pomo_hid] disconnected conn=0 reason=0x13
[pomo_hid] ADV started status=0 mode=0
[pomo_hid] connected conn=0 peer=28:d0:43:82:eb:ed intv=39
[pomo_hid] ADV stopped reason=0 mode=0
[pomo_hid] link encrypted
```

Reconnect succeeded on the existing bond and advertising restarted automatically; `reason=0x13` on disconnect. Zero I2C errors in this window too.

Measurement note: `reconnect-uart.txt` was empty on the first attempt because pyserial raises `RTS` when it opens the port, which holds this board in reset. All captures above were taken with `RTS` and `DTR` explicitly deasserted after opening. The first empty capture is kept as `reconnect-uart.txt` history only in this note; the file was overwritten by the corrected run.

## 6. Not exercised — still pending physical interaction

No agent step in this pass performed or observed any of the following, and no result is claimed:

- touchscreen taps and the visible UI (W1 stand hub, W2 transitions/theme, W3 flat memo page, W5 side panel rendering, disabled-button look, transient feedback bar);
- timer completion sound (`BEEP`) audibility and long-press acceleration feel;
- microphone capture, elapsed-duration display, and SPK playback of a recording;
- TF-card mount, FILE cycle, playback progress, and two-step `CONFIRM?` delete — no card was inserted;
- media-control commands actually reaching a host (W4 delivery). Task 12 is archived on the user's 2026-11-08 statement that the task passed, recorded in `.workflow/done/pose-driven-ui/12-ble-media-controls.md`; there is no agent per-item log of the six keys and it was not re-run here.

## 7. Reproduction

```sh
source /home/shizuku/.sifli/SiFli-SDK/main/export.sh -t gcc
make -C capsule test
cd project && scons --board=sf32lb52-lchspi-ulp -j8
cd build_sf32lb52-lchspi-ulp_hcpu && sftool --connect-attempts 1 -p /dev/ttyUSB0 -c SF32LB52 -m nor write_flash --verify \
  "bootloader/bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab/ftab.bin@0x12000000"
# capture at 1000000 baud with RTS deasserted; pulse RTS high 0.5 s then low to reset
```

The throwaway capture helper used for this pass was moved out of the repository to `/tmp/task13-serial-capture.py`; it is not part of the deliverable.

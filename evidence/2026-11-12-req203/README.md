# REQ-203 2026-11-12 证据（真机 S2–S6）

> 注：目录里的 `.wav` 是真实录音，**未纳入版本库**（体积/隐私）。需要复现请接板子后运行
> `python tools/ble_memo_sync/sync.py --pull --out evidence/2026-11-12-req203/memos`；
> 下面的日志、字节数、SHA256 与产物均可独立核对。

## 1. 板端接入与烧录（S2）

- `src/main.c`：新增 `memo_tf_busy()`，`ble_hid_init()` 之后调用 `ble_file_init(memo_tf_busy)`；
  `BLE_POWER_ON_IND` 只置 flag，服务注册与 MTU 交换在工作线程执行（否则上电流程卡死）。
- 构建 `SCONS_EXIT=0`，`main.bin` 905,432 B；`sftool write_flash --verify` → `FLASH_EXIT=0`。
- 串口（`boot-serial.txt`）：
  ```
  [blefile] registering service...
  [blefile] service handle=2002d28c
  [blefile] svc ready (svc=c0de0001 cmd=c0de0002 data=c0de0003)
  [pomo_hid] ADV started status=0 mode=0
  ```

## 2. S3/S4：版本与列表

bleak 3.1.0，MTU 527：`V -> V1 13`；`L -> F memo0000.wav … F memo0012.wav 960044, OK`。

## 3. S5：12 条短录音全量拉取（`pull.log`）

`sync.py --pull --out evidence\2026-11-12-req203\memos` → 12 条 / 975 KB，
每条字节数与板上列表一致，全部为 16 kHz/16 bit/单声道合法 WAV。

## 4. S6：30 s 整条（`pull30.log` + `serial-30s.txt`）

- 文件：`memo0012.wav`，960,044 B（30.0 s）。
- 板端日志：`[blefile] sent memo0012.wav 960044 B in 14009 ms` → **≈68.5 KB/s**（5,000 帧 × 192 B）。
- 主机端到端（含连接/服务发现/订阅）：17.3 s（第二次）与 26.0 s（第一次）。
- 结果：`memos30/memo0012.wav` 960,044 B，WAV 头 16 kHz/16 bit/单声道/30.0 s，SHA256 `c4a7b5ca…`。

## 5. 主机离线端到端（`sync-e2e.txt`）

960,044 B 帧流：MTU 247（8,003 帧）、MTU 517（5,003 帧）SHA256 一致；MTU 23 用 96 KB 样本（12,002 帧）。

## 6. 说明

板端不返回自身 SHA256，验收依据是“列表尺寸一致 + `E` 帧完整 + WAV 头合法 + 文件可解码”。

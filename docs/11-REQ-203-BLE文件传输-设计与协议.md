# REQ-203 蓝牙传文件（设计 + 协议）

| 项 | 内容 |
|---|---|
| 文档编号 | DOC-11 |
| 关联需求 | REQ-203「蓝牙把 WAV 同步到电脑或手机」 |
| 状态 | 板端 S1–S6 全部真机通过：`V/L/G`、12 条短录音全量 + 30 s 整条（板端 14.0 s ≈ 68.5 KB/s），HID 同机正常 |
| 日期 | 2026-11-11（2026-11-12 改为自建 GATT 服务） |

## 1. 方案选择

| 方案 | 结论 |
|---|---|
| **自建 GATT 服务 + 自定义 ATT 表**（128-bit 私有 UUID，`cmd` 写 + `data` 通知） | ✅ 采用（2026-11-12）。每行一次 notify、`\n` 分帧，不需要 ATT 层重组，工作量可控 |
| 复用 SDK 串口透传服务（`bf0_sibles_serial_trans_service`） | ❌ 否决（2026-11-12）。字符设备封装 `rt_ble_serial_dev_register` 在 `bf0_sibles_console.c`，开启即把 console 切到 BLE，破坏串口调试 |
| USB MSC | 板上 USB 只接 CH340，不可行 |
| WiFi | 芯片无 WiFi |

服务信息（自建，写死在 `src/ble_file.h` 与 `src/ble_file.c`；实施方案见 docs/12）：

| 项 | 值 |
|---|---|
| Service | `c0de0001-5f1f-4b1e-9c3a-706f6d6f0001` |
| `cmd` 特征 | `c0de0002-…`，`WRITE_REQ`/`WRITE_COMMAND`，收 `V` / `L` / `G <name>` |
| `data` 特征 | `c0de0003-…`，`NTF` + CCCD，回 `V1/F/OK/B/D/E/X` |
| 安全等级 | `NO_AUTH`（免密，与 HID 一致） |
| 广播 | 不改，仍广播 `POMO_MEMO_HID`；主机连上后按 UUID 找服务 |

## 2. 协议（文本行，`\n` 结尾，ASCII）

板端 → 主机与主机 → 板端都走同一条服务通道（`cmd` 写命令、`data` 收通知）。

| 方向 | 报文 | 含义 |
|---|---|---|
| → 板 | `V` | 查询版本 |
| 板 → | `V1 <count>` | 协议版本 1 + 录音条数 |
| → 板 | `L` | 列出录音 |
| 板 → | `F <name> <size>`（可多条）→ `OK` | 文件名与字节数 |
| → 板 | `G <name>` | 拉取文件 |
| 板 → | `B <name> <size>` | 开传，声明总字节 |
| 板 → | `D <hex>`（多条，每条 ≤192 字节原始数据） | 数据块，十六进制 |
| 板 → | `E` | 传输结束 |
| 板 → | `X <code>` | 错误：`1` 无此文件、`2` 读失败、`3` 参数错、`9` 忙 |

约定：

- 文件名只允许 `memoNNNN.wav`（与板端 `tf_library` 一致），主机侧必须做白名单校验，防目录穿越。
- 十六进制而非 base64：多 50% 流量，但无需查表、无需担心行内含 `\n`，实现最稳。30 s 录音（960 KB）→ 1.9 MB 文本，BLE 实测 10–50 KB/s，约 40–190 s。
- 主机按 `size` 校验收齐字节数，不齐则丢弃该文件。

## 3. 主机端工具

`tools/ble_memo_sync/sync.py`：

```powershell
python tools\ble_memo_sync\sync.py --selftest          # 离线自检协议编解码
python tools\ble_memo_sync\sync.py --scan              # 列出附近 BLE 设备与服务
python tools\ble_memo_sync\sync.py --pull --out memos   # 连接并拉取全部录音
```

依赖 `bleak`（`pip install bleak`）；`--selftest` 不需要任何依赖。

## 4. 板端实现（`src/ble_file.c`）

已按 docs/12 落地，结构与本仓库 `ble_hid.c` 一致：

1. `ble_file_init(bool (*busy)(void))` 建信号量 + 工作线程；`BLE_POWER_ON_IND` 时 `sibles_register_svc_128()` 注册服务；
2. `cmd` 的 set 回调只按字节累积成行、入队（4 条环形队列），不读盘；
3. 工作线程 `memo_ble` 解析 `V/L/G`：`V` 数一遍目录，`L` 用 `opendir("/")` 过滤 `memoNNNN.wav` 并用 `dfs_file_stat` 取字节数，`G` 用 `dfs_file_read` 按 `(mtu-7)/2`（≤192 B）分块读并转 hex；
4. 互斥：录音/回放忙或已有传输进行中回 `X 9`；传输期间 `ble_file_transfer_active()` 为真；
5. `main()` 侧只需一行 `ble_file_init(busy_fn)`（S2 接入，见 docs/12 第 4 节）。

## 5. 验证计划

| 层 | 手段 | 状态 |
|---|---|---|
| 协议编解码 | `sync.py --selftest`（组帧/拆帧/尺寸校验） | ✅ `ok 7 checks`（2026-11-11） |
| 主机离线端到端 | `sync.py --selftest-e2e --mtu 247/517`（960,044 B + 随机分片 + SHA256） | ✅ 2026-11-12，三种 MTU 全部一致 |
| 板端编译 | `scons --board=sf32lb52-lchspi-ulp -j8` | ✅ `SCONS_EXIT=0`（S1 模块、S2 接入后各一次） |
| 真机 `V`/`L` | Windows bleak 3.1.0，MTU 527 | ✅ `V1 12`、12 条 `F …` + `OK`（2026-11-12） |
| 真机全量拉取 | `sync.py --pull` | ✅ 12 条 / 975 KB 全部拉下，尺寸与列表一致（2026-11-12） |
| 30 s 整条 | `sync.py --pull --only memo0012.wav` | ✅ 960,044 B：板端 14.009 s（≈68.5 KB/s），主机端到端 17.3–26.0 s（MTU 527，5,000 帧） |

## 6. 现状说明

- 板端：`src/ble_file.c` 已接入 `main()`、烧录并真机跑通 S2–S5；`[blefile] svc ready` 与 HID 广播同时正常。
- 关键坑：**服务注册必须离开 BLE 事件回调线程**（否则上电流程卡死、HID 也不初始化），见 `docs/12` R7。
- 主机端：`sync.py --pull` 之前有“异步生成器传给同步解析”的缺陷（`--selftest` 测不到），已在真机上暴露并修复。
- 仍未验证：无（S2–S6 均已在真机完成）；可选增强是让板端自己算 SHA256（当前协议没有该命令）。

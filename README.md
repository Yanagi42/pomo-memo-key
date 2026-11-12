# 闪念胶囊

黄山派上的桌面番茄钟，也是按一下就能录音的便签。需求在 `docs/01-需求规格说明.md`。不依赖板子的逻辑在 `capsule/`，电脑上可以先跑：

```bash
make -C capsule test
```

固件工程在 `project/`，应用入口在 `src/main.c`。当前代码基于 `pose-driven-ui` 规范接入五姿态参照、STAND 三模式中心、微晶亮色/暗夜护眼全局双主题、450×390 全画布无边框设计、KEY2 闪念按键、板载 MEMS 麦克风录音、TF 卡录音库与 BLE HID：
- 直立姿态（STAND）三模式中心：开机默认处于桌面时钟（TIME），支持顶部药丸切换栏与水平滑动手势（>40 px）在 POMO（番茄钟）、TIME（桌面时钟）、STOPWATCH（正向秒表）间平滑切换，RAM 保持上次选择记忆；POMO 支持 1–120 分钟调节（1 分钟步进、长按阶梯加速）、BEEP 独立开关与 500 ms 异步蜂鸣、仅立起且在 POMO 子页时自动开始/离开暂停（停留在 TIME 或 STOPWATCH 时立起不偷跑）；TIME 基于片上 RTC 实时显示 HH:MM、基线对齐秒与 12H/24H 切换，并提供月亮键切换全局主题；STOPWATCH 支持 0.1 秒精度跑秒与计次胶囊。
- 放平姿态（FLAT）语音便签：上方内凹卡片显示文件名（MEMO_XXXX.WAV / MEMO_RAM.WAV）、7 柱动态波形胶囊条（待机低矮、录放起伏、录音变红）、当前时长与 00:30 上限、只读胶囊进度条；下方四键操作栏（文件夹/TF切源、▶/■ 播放停止、● 录音停止、🗑 删除 CONFIRM? 二次确认，支持 RAM 与 TF 录音确认删除）；录音已移除 VAD 静音裁剪，完整保留 16 kHz 16-bit 单声道 raw PCM；无卡或写失败时回退 RAM 存储并给出未持久化提示；回放由独立工作线程异步执行并带防重入保护。
- BLE Consumer Control HID：支持音量加、音量减、系统音频静音（不发麦克风静音）、上一首、播放/暂停、下一首六项控制；左右侧放多媒体遥控页统一呈现 DESK·02 界面，包含 CONNECTED/OFFLINE 胶囊、3×2 图标按键与 1.5 秒即时反馈条，未连接显示禁用态；保留 KEY2 侧放宏；用户于 2026-11-08 确认任务 12 验收通过；本轮未重复媒体控制实测（本次板端见证连接与加密断开重连）。
- 屏幕基于 LVGL 采用 450×390 全画布无边框设计（物理 390×450 旋转 270°），消除默认主题残余边框与边缘横线，移除调试标签；微晶亮色与暗夜护眼全局双主题在四页面统一生效；姿态稳定 400 ms 触发 260 ms ease-out 滑动转场，转场中换姿态平滑重定向，未知保持原页，扣下熄屏静音、恢复点亮。板级引脚按官方表配置为传感器 I2C1（PA40/PA39）与 FT6146 触控（PA37/PA33）独立映射；此前列出的显示/触控、姿态与主题、发声等手动项经用户整体实测确认可用（无逐项 agent 日志）。
- 姿态启动使用 2026-11-05 在本板采集的五姿态参照，开机不重复标定。
- 录音期指示：录音中任意页面左上角显示呼吸红点，扣下熄屏时隐藏，不遮挡触摸。
- REQ-203 蓝牙传文件：板端自建 128-bit GATT（`src/ble_file.c`，`V/L/G` + hex 分帧）；主机端 `tools/ble_memo_sync/sync.py`（`--selftest`、`--selftest-e2e`、`--pull`、`--only`）。2026-11-12 真机通过：`V1 13`、13 条列表、12 条全量 + 30 s 整条（板端 14.0 s ≈ 68.5 KB/s）。
- REQ-204 转写待办：PC 端 `tools/todo_pipeline/todo_pipeline.py`（`--selftest` 13 项；`--input` 支持任意 16 kHz/16 bit/单声道 WAV）。2026-11-12 真机闭环：板上 `memo0013.wav`（7.27 s）→ 转写“明天要教软件实践的测试报告。” → 1 条待办。

板型是 **sf32lb52-lchspi-ulp**。课程基线和本机实际 SDK 均为 v2.5.1。SF32LB52 Rev 3 运行 BLE 依赖 RAM 中的 LCPU 镜像；`project/SConstruct` 必须通过 `AddLCPU(SIFLI_SDK, rtconfig.CHIP, "../src/lcpu_img.c")` 预编译 SDK `lcpu_general` 并将 `main.lcpu` 嵌入应用镜像，避免因 SDK 弱符号空实现导致 LCPU 控制器无法启动。在已经执行 SDK export 的 Bash 终端里：

```bash
cd project
scons --board=sf32lb52-lchspi-ulp -j8
```

初次验收（修复前）历史记录：对应统一固件版本 v0.4.2。源码工程基于 SiFli SDK v2.5.1 构建成功（exit 0），生成 `main.bin` 896,600 字节（ROM 947,587 B，RAM 205,244 B），主机单元测试 `make -C capsule test` 通过 389 checks（exit 0）。固件经 `sftool write_flash --verify` 刷写（exit 0）并通过 RTS 脉冲复位启动，两次启动捕获（25s/18s）及重连捕获（35s）均确认自检通过（`capsule selftest PASS (0)`）、IMU 随动/FT6146/UI 就绪、未插卡开机回退 RAM、I2C 错误计数为 0、BLE 广播开启并完成一次断开后加密重连（agent 串口观测，证据见 `evidence/2026-11-08-task13/`）。用户于 2026-11-08 确认任务 12 验收通过；此前待验的物理屏幕视觉、触控交互、发声听感与插卡启动后的存取/重启保留等手动项，已于 2026-11-08 经用户整体实测确认可用（无逐项 agent 日志）。

2026-11-08 跟进修复与刷写：针对用户报告的 TF 卡未识别与 RAM 无法删除缺陷，已在 `project/proj.conf` 增加 `CONFIG_RT_DFS_ELM_USE_EXFAT=y` 修复 exFAT 挂载失败（`f_mount=13`），并在 `src/main.c` 补齐 RAM 录音删除分发。新固件已构建成功并经 `sftool write_flash --verify` 刷入板端。板端实测确认：256GB TF 卡（`sd0`）硬件识别与 exFAT 挂载成功（`disk free: 232.9 GB`，根目录 `ls` 正确），自建测试文件写入、回读与重启后持久化保留全部通过，测试后已清理恢复；主机单元测试通过 420 checks（exit 0），真实 `handle_ui_commands` 提取的主机 harness 12 步删除逻辑验证通过（证据见 `evidence/2026-11-08-tf-ram-fix/`）。此前列出的实机物理屏幕触摸点击删除、界面视觉反馈、真实录放音质及 TF 文件持久化等手动项，已于 2026-11-08 经用户整体实测确认可用（证据来源为用户整体口头确认，无逐项 agent 测试日志）。

## 已知外观缺陷

屏幕右侧约 60 px 绿条（物理竖屏 390×450，LVGL 270° 旋转只输出 390×390 画面）。原版 v0.4.2 即存在，本轮未修；台账见 `docs/06` 的 P-04。把面板直接改成 450×390 会花屏（面板 RAM 只有 390 列），需要改旋转/刷新合并策略。

## 拉录音与转写（REQ-203 / REQ-204）

```powershell
python -m pip install bleak faster-whisper
python tools\ble_memo_sync\sync.py --pull --out memos      # 从板上拉全部录音

$env:HF_ENDPOINT='https://hf-mirror.com'                   # 国内网络下载模型用镜像
$env:WHISPER_MODEL='small'                                 # 默认 tiny 更快但中文更差
python tools\todo_pipeline\todo_pipeline.py --input memos\memo0011.wav --transcriber whisper
```

板子的 BLE 文件服务 UUID 是 `c0de0001-5f1f-4b1e-9c3a-706f6d6f0001`（`cmd` 写、`data` 通知），
广播名仍是 `POMO_MEMO_HID`。真机验收记录见 `evidence/2026-11-12-req203/` 与 `evidence/2026-11-12-req204/`。

# 闪念胶囊

立创黄山派上的桌面番茄钟，也是按一下就能录音的便签。立稳开始倒计时，扣下去就暂停、静音、熄屏。想记一句时按 KEY2，不必先看屏幕。录音留在板上，电脑再拉下来转成文字。

板子是 **sf32lb52-lchspi-ulp**（SF32LB52）。蓝牙广播名是 `POMO_MEMO_HID`。固件用 SiFli SDK v2.5.1 编译，SDK 不在本仓库里。

姿态靠板上的加速度计判断。同一个姿势保持约 400 毫秒才算数。五个姿势的参照值已经写在固件里，开机不用再标定。

## 板上

**直立**时屏幕是计时页，开机停在时钟。点顶部三个标签可以切页，左右滑动超过约 40 像素也会切页：

- **POMO**：倒计时，1 到 120 分钟，每次调 1 分钟。按住加号或减号会越来越快。停在这一页且板子立稳时，计时自动开始；离开直立，或切到另外两页，计时暂停。点播放键也可以开始或继续。走到 0 之后，一直立着不会重新开始，要先离开再立回来。铃铛键控制结束时响不响一声。插着 TF 卡时，走完会往 `pomo.log` 追加一行 `done`；计时中按刷新停下，会按已经走过的分钟数追加一行 `stopped`。
- **TIME**：片上时钟，可以切换 12 小时制和 24 小时制，也可以显示或隐藏秒。年份早于 2026 时，开机把时钟写成 2026-11-08 10:42。`NIGHT` 在浅色和深色之间切换，四个页面一起变。
- **STOPWATCH**：正向秒表，显示到 0.1 秒，可以记最近三次。

**平放**是录音页。上面显示当前文件名、七柱波形、已录时长，上限 30 秒。下面是四个键：下一个文件、播放或停止、录音或停止、删除。删除要再点一次才真正删掉。

只要不在侧放遥控状态，按 KEY2 也能开始或停止录音，松手不会停。录音格式为 16 kHz、16 bit、单声道，停止后整段保留，不裁掉静音。单段最长约 30 秒，放不下就停并标明没写完。插着能挂载的 TF 卡时（需为 exFAT），文件存成 `memo0000.wav` 这类名字。没插卡或写入失败时，录音留在板子内存里，掉电即失，屏幕上会标明没存住。录音进行时，每个页面左上角有一个红点；扣下熄屏时红点也隐藏。

**左侧或右侧放在桌上**时，屏幕是同一套多媒体遥控：音量加、音量减、上一首、下一首、系统静音、播放或暂停。电脑要先在蓝牙里配对 `POMO_MEMO_HID`。还没连上时，按键不会发出命令。已经在桌上立过、平放过或扣过之后，侧放时 KEY2 改发遥控：左侧短按是音量加，右侧短按是下一首，任一侧按住约 600 毫秒是系统静音。这里的静音是电脑的系统静音，不是麦克风静音。

**扣下**时暂停计时、关掉提示音、屏幕变黑。

录音或播放还没停时，板子会拒绝蓝牙传文件，避免争抢 TF 卡读写。

## 电脑

`desktop/summary_app` 是 Windows 上的「闪念总结」。板子要已经在系统蓝牙里连着，名字里带 `POMO_MEMO`。

在窗口里点「读取设备」，会把卡上的 `memoNNNN.wav` 和 `pomo.log` 拉到 `desktop/summary_app/from_device`，再用本机的 faster-whisper（模型 `small`）转写为同名 `.txt` 并转为简体。听不出人声的录音，文本留空。密钥、接口地址、模型名称三个都填了，才会把这份汇总发给兼容 OpenAI Chat Completions 的接口；留空就只在本机列出来。接口地址可以写到 `/v1`，程序会自己补上 `/chat/completions`。

```powershell
python -m pip install bleak faster-whisper opencc numpy
python desktop\summary_app\app.py
python desktop\summary_app\app.py --selftest
```

转写模型第一次运行时会下载。「选择目录」可以不连板子，直接汇总一个已经有 `memo*.wav` 和 `pomo.log` 的文件夹。这种读法不会重新转写，同名 `.txt` 有内容才会显示文字。

`pomo.log` 每行四项，用空格分开：

```text
2026-11-12 10:42 25 done
2026-11-12 14:00 10 stopped
```

依次是日期、时间、分钟、结果。结果只有 `done` 和 `stopped`。

另外两个命令行脚本不经过这个窗口：

- `tools/ble_memo_sync/sync.py` 只拉 `memoNNNN.wav`。`--selftest` 不需要板子，也不需要安装 bleak。
- `tools/todo_pipeline/todo_pipeline.py` 把这些 WAV 整理成待办。用法在 `tools/todo_pipeline/README.md`。

板端文件服务的 UUID 是 `c0de0001-5f1f-4b1e-9c3a-706f6d6f0001`。协议在 `docs/11-REQ-203-BLE文件传输-设计与协议.md`。

## 目录

| 路径 | 是什么 |
| --- | --- |
| `capsule/` | 计时、姿态、录音的逻辑，不依赖板子。`make -C capsule test` |
| `project/` | 固件工程 |
| `src/main.c` | 板端入口：界面、按键、录音 |
| `src/ble_hid.c` | 多媒体遥控 |
| `src/ble_file.c` | 把录音和 `pomo.log` 传出来 |
| `src/tf_library.c` | TF 卡上的录音库 |
| `desktop/summary_app/` | 拉录音、转写、汇总 |
| `docs/` | 需求、设计和测试记录 |

## 编译

在已经执行过 SDK 环境脚本的终端里：

```bash
cd project
scons --board=sf32lb52-lchspi-ulp -j8
```

这块芯片跑蓝牙时，需要一份放在 RAM 里的 LCPU 镜像。`project/SConstruct` 里的 `AddLCPU(SIFLI_SDK, rtconfig.CHIP, "../src/lcpu_img.c")` 会把它编进应用。删掉这一行，蓝牙控制器起不来。

本机 SDK 路径和工具版本记在 `docs/environment.md`。

## 已知问题

屏幕右侧有大约 60 像素宽的绿条。物理屏是竖向 390×450，界面转了 270° 之后只画出 390×390。把面板直接改成 450×390 会花屏，因为面板 RAM 只有 390 列。记录在 `docs/06-风险与问题台账.md` 的 P-04。

## 许可

SiFli SDK 的 Apache-2.0 文本在 `LICENSE-SiFli-SDK-Apache-2.0.txt`。本仓库不包含 SDK 本身。

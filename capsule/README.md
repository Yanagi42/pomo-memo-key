# 闪念胶囊

在桌上立起时开始 25 分钟倒计时，扣下时暂停。按 KEY2 录音，停止录音时去除长段静音，保存为 16 kHz 单声道 WAV 文件。在桌上侧放时，左侧短按是音量加，右侧短按是下一首，长按是麦克风静音。

需求、设计和测试记录见 `docs/prd.md`、`docs/design.md` 和 `docs/test-report.md`。

## 这台机器上能跑的

```bash
make -C capsule test
```

测试通过时会打印 `ok 178 checks`。该测试不需要连接开发板，也不链接 SiFli SDK。

## 还要你做的

1. 向老师领取 `course_starter`。不要把 `peripheral_lab` 整份复制进产品工程。
2. 在有 SDK v2.5.1 的环境里编译。本仓库不包含 SDK。
3. 把 `capsule/src` 里的源文件加入工程。业务逻辑不要放进中断，音频回调中只拷贝 PCM 数据。
4. 界面只能由 LVGL 任务操作。录音缓冲区按最多 30 秒计算，大约 960 KB，要放入 PSRAM，不要放进任务栈。PCM 和 WAV 缓冲都要按 2 字节对齐。裁剪标记约 4 KB，放在静态区。录音任务的栈空间建议不小于 8 KB。
5. IMU 大约每 100 ms 读取一次。相邻两拍超过 400 ms 时，姿态判定不会稳定，倒计时也不会开始。原始值用 `capsule_accel_millig` 换成毫 g。时钟要向前走，倒退的时间戳会被丢掉，这一拍的加速度也不改。
6. 首次使用前先做标定。五个姿势分别拿稳，等屏幕上的 x/y/z 读数不再晃动时调用 `capsule_capture`：直立、扣下、左侧、右侧、平放。还没有样本，或这一拍不在约 0.4 g 到 2.5 g 时，它返回 -1，不覆盖已有参照。要跨掉电记住，把 `capsule_pose_map_export` 的 64 字节写入闪存，`capsule_init` 之后再 `capsule_pose_map_import`。全零的槽表示还没标定。槽里若不是静止重力，整份导入失败，原表不动。
7. KEY2 引脚为 PA43。在课程示例中按下为高电平。做好按键消抖后再调用按下和松开接口。
8. 麦克风参数参考 `peripheral_lab` 的录音测试：16 kHz、单声道、16 bit、`AUDIO_MIC0_ONLY`，并关闭上行 AGC。停止录音且状态为已保存时，用 `capsule_wav_read` 取出 PCM 播放。`capsule_take_beep` 为真时播放一声短提示音。
9. 插上开发板后，按照 `docs/test-report.md` 的 B1 到 B9 逐项测试，并把测试现象记录回文档。不要把未执行的板端测试写成通过。
10. 若放在口袋中使用需自备电池。如果想掉电后保留录音，后续再接 TF 卡；如果想真正控制电脑音量，后续再做蓝牙 HID；如果想整理成待办事项，可以先把 WAV 文件传到电脑上，再接入转写和模型。这三项都属于拓展功能。

喇叭只能插在板上丝印为 SPK 的接口上。

## 调用顺序

各接口统一使用同一个毫秒时钟计数。

```c
capsule_init(box, pcm_buffer, pcm_samples, wav_buffer, wav_bytes);
/* 闪存里已经有 64 字节时再导入。第一次使用跳过这一行。 */
capsule_pose_map_import(&box.map, saved, CAPSULE_POSE_MAP_BYTES);

/* 标定页：姿态拿稳之后。返回 0 才算记下。 */
capsule_on_accel(box, ax_mg, ay_mg, az_mg, now_ms);
capsule_capture(box, CAPSULE_POSE_STAND);

/* 运行中 */
capsule_on_accel(box, ax_mg, ay_mg, az_mg, now_ms);
capsule_on_time(box, now_ms);
capsule_on_key_down(box, now_ms);
capsule_on_key_up(box, now_ms);
capsule_on_pcm(box, pcm, count);
capsule_format_status(box, text, sizeof text);
```

状态行示例：`直立 24:59 x=0.020 y=-0.040 z=0.998`

下一次开始录音时会清除上一份 WAV 文件。如果界面要保留文件，请在状态变为已保存时及时拷贝。

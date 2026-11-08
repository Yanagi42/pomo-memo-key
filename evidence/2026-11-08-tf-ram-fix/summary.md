# 2026-11-08 TF 检测与 RAM 删除修复 — 证据总结

## 1. 验证事实

| 项 | 动作 / 执行命令 | 结果 | 证据文件 | 观测结果 |
|---|---|---|---|---|
| RAM 录音删除冒烟测试 | 基于 `src/main.c` 真实 `handle_ui_commands` 提取逻辑运行 host harness | 全部通过 (12/12 checks, exit 0) | `ram-delete-smoke.txt` | 首次点击进入 `CONFIRM?` 状态；二次点击确认删除并调用 `capsule_memo_clear` 清空 WAV 及录音状态，重置时长为 0 与有效标志；空录音安全退出并报告反馈；录音活跃及回放活跃时拒绝删除；切换文件、播放、录音操作自动取消待确认状态。 |
| 主机单元回归测试 | `make -C capsule test` | 全部通过 (420 checks, exit 0) | 既有测试套件 | capsule 核心状态机及回归测试全部通过。 |
| 修复固件工程构建 | `source .../export.sh && cd project && scons --board=sf32lb52-lchspi-ulp -j8` | 构建成功 (exit 0) | `build-exfat.log` | 包含 RAM 删除补丁与 `CONFIG_RT_DFS_ELM_USE_EXFAT=y`，生成 `main.bin` 902,176 B（HCPU ROM 953,271 B，RAM 205,244 B；LCPU 6,432 B；存在既有基线 newlib/ftab 链接 warning）。 |
| 固件校验刷写 | `sftool write_flash --verify` | 烧录校验成功 (exit 0) | `exfat-flash-verify.log` | 全部写入与校验阶段达 100%。 |
| TF 硬件识别与挂载 | 带卡复位启动，串口观测启动日志与 msh `df` / `ls` | 识别挂载成功 (exit 0) | `exfat-firmware-with-card.txt`, `exfat-mount-ls-df.txt` | 识别 `sd0` 容量 232.96 Gbyte，FatFs 成功挂载 exFAT；输出 `[course_starter] TF library ready`；`df` 显示 `disk free: 232.9 GB`，根目录识别 `System Volume Information`。 |
| TF 文件读写实测 | msh 写入 17 字节测试文件 `tf_probe_20261108.txt` 并读取 | 读写成功 (exit 0) | `exfat-rw-step1.txt` | 根目录列出 17 字节 `tf_probe_20261108.txt`，读取内容为 `TF_PROBE_20261108`。 |
| TF 重启持久化验证 | RTS 脉冲复位开发板，启动后再次读取测试文件 | 验证通过 (exit 0) | `exfat-rw-step2-reboot.txt` | 重启后再次读取该文件完整输出 `TF_PROBE_20261108`，确认写入在冷启动后持久保存。 |
| TF 探针文件清理 | msh 执行 `rm` 清理自建文件，复查根目录与剩余容量 | 清理完成 (exit 0) | `exfat-probe-cleanup.txt` | 根目录仅留原始 `System Volume Information`，自建探针已完全移除，剩余容量仍为 232.9 GB。 |

## 2. 状态与验证边界

- **固件与板端验证状态**：新固件已成功构建并刷入开发板；TF 卡的驱动识别、exFAT 挂载、文件读写、跨重启持久化及探针清理已在实机串口/msh 完全实测通过。
- **UI 与实机交互边界（如实保持待用户统一手动验收）**：
  1. RAM 删除逻辑与安全保护已由主机 harness 完整验证，但在物理屏幕上通过触摸点击垃圾桶按钮删除录音的行为尚未进行人工实测；
  2. TF 卡已证明可正常读写与持久化，但录音生成实际 WAV 文件的音频质量听感与扬声器回放声音尚未进行人工实测，不将 W3 全部判定为通过；
  3. 四页面全画布无边框视觉效果、双主题切换与手势滑动仍保持待用户手动实测验收。

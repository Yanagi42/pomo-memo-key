# REQ-204 2026-11-12 证据（板上真实录音 + faster-whisper）

> 注：`11.wav`、`memos-new/*.wav` 等真实录音**未纳入版本库**（体积/隐私）。
> 复现方式：先按 REQ-203 用 `sync.py --pull` 拉取，再运行下面的转写命令。

## 1. 输入

| 文件 | 来源 | 长度 | 说明 |
|---|---|---|---|
| `11.wav` | 板上 `memo0011.wav`（REQ-203 拉取） | 3.92 s / 125,484 B | 用户所说的 11.wav |
| `../2026-11-12-req203/memos30/memo0012.wav` | 用户新录的 30 s | 30.0 s / 960,044 B | S6 用；第 1 秒 RMS≈1562，其余 29 秒 RMS≈22（近静音） |
| `memos-new/memo0013.wav` | 用户新录（本轮闭环） | 7.27 s / 232,684 B | 转写 → 1 条待办；`--pull --only` 9.28 s |

## 2. 真实转写

```powershell
python -m pip install faster-whisper
$env:HF_ENDPOINT='https://hf-mirror.com'   # 直连 HuggingFace 超时
$env:WHISPER_MODEL='tiny'                  # 30 s 那条用了 small
python tools\todo_pipeline\todo_pipeline.py --input evidence\2026-11-12-req204\11.wav `
    --transcriber whisper --extractor rules --out evidence\2026-11-12-req204\out-whisper
```

| 文件 | 模型 | 转写 | 待办 |
|---|---|---|---|
| 11.wav（3.92 s） | tiny / CPU int8 | “完成 这门口尝的时间报告” | 0（无触发词） |
| memo0012.wav（30 s） | small / CPU int8 | “完整本新言報告然後” | 0（29 s 近静音） |

12 条整目录也跑过（`out-all/`、`whisper-all.log`），均为 0 条待办。

## 4. 真实待办闭环（2026-11-12 01:05）

- 新录音：`memos-new/memo0013.wav`，232,684 B（7.27 s），`--pull --only` 耗时 9.28 s。
- `WHISPER_MODEL=small` + `WHISPER_PROMPT`（简体提示）→ `text: "明天要教软件实践的测试报告。"`。
- 规则抽取 → **1 条待办**：

```markdown
# 语音待办（2026-11-12 01:06）

- [ ] 明天要教软件实践的测试报告
      <!-- memo0013.wav · 11-12 01:05 · 7s -->

共 1 条录音，1 条待办。
```

- 说明：同音字“交→教”是模型精度问题；加提示词前输出为繁体（`軟件實踐`），现已稳定简体。
- 产物：`out-new/todos.md`、`out-new/transcript.json`；日志 `whisper-13.log`。

## 5. 本轮修掉的缺陷

| 缺陷 | 现象 | 修复 |
|---|---|---|
| whisper `device="auto"` 选 CUDA | 无 cuBLAS → `RuntimeError: Library cublas64_12.dll is not found` | 默认 `cpu` + `int8`，可用 `WHISPER_DEVICE`/`WHISPER_COMPUTE` 覆盖 |
| mock 读旁注 BOM | 第一条待办前出现 `\ufeff` | 改 `utf-8-sig`，`--selftest` 增加 BOM 用例（12 → 13 checks） |
| 中文输出为繁体 | `軟件實踐`，影响阅读 | `transcribe()` 增加 `initial_prompt`（可用 `WHISPER_PROMPT` 覆盖），实测稳定输出简体 |

## 6. 结论与下一步

- 已闭环：板端录音 → BLE 拉取 → 格式校验 → 转写 → 规则抽取 → `todos.md`（1 条待办，带溯源）。
- 仍可提升：同音字（交/教）与低音量录音。建议离麦克风近一些、说完再停；追求准确率可换 `WHISPER_MODEL=medium` 或云端 `--transcriber openai`。

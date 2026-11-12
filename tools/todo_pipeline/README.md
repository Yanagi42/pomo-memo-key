# 语音待办流水线（REQ-204）

把板子录的 WAV 便签转成待办清单。**纯 PC 侧工具，不改固件**。

## 用法

```powershell
# 0) 离线自检（不碰任何文件）
python tools\todo_pipeline\todo_pipeline.py --selftest

# 1) 零依赖：从 TF 卡目录直接出待办（mock 转写 + 规则抽取，只适合演示）
python tools\todo_pipeline\todo_pipeline.py --input D:\memos --out out

# 2) 本地转写 + 规则抽取（需要 pip install faster-whisper）
python tools\todo_pipeline\todo_pipeline.py --input D:\memos --out out `
  --transcriber whisper --extractor rules

# 3) 云端转写 + 大模型整理（需要 OPENAI_API_KEY）
$env:OPENAI_API_KEY="sk-..."
python tools\todo_pipeline\todo_pipeline.py --input D:\memos --out out `
  --transcriber openai --extractor llm
```

输出：

- `out/todos.md` —— `- [ ]` 勾选式待办，每条带 `<!-- 文件名 · 时间 · 时长 -->` 以便回溯
- `out/transcript.json` —— 每条录音的转写原文、时长与待办

## 输入要求

`memo*.wav`，**16 kHz / 16 bit / 单声道**（与板端 `tf_library` 的判定完全一致）；格式不符的文件会被跳过并在 stderr 提示。

## 环境变量

| 变量 | 作用 | 默认 |
|---|---|---|
| `WHISPER_MODEL` | faster-whisper 模型名 | `small` |
| `TODO_MODEL` | 整理待办用的模型 | `gpt-4o-mini` |
| `OPENAI_API_KEY` | 云端转写/整理所需 | 无 |

## 设计说明

- 只用标准库；`faster_whisper` / `openai` 仅在显式选用时才 import，缺库会给出可执行的提示。
- 离线模式（`--extractor rules`）用触发词（要/需要/记得/明天/提醒/TODO…）+ 句子切分 + 去重，中文英文都能出结果。
- 不做实时转写，也不改板端固件；板子只负责把 WAV 落在 TF 卡上。

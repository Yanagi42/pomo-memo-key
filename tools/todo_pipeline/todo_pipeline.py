#!/usr/bin/env python3
"""REQ-204：把板子录下的 WAV 便签转写并整理成待办清单。

输入是 TF 卡上的 memoNNNN.wav（16 kHz / 16 bit / 单声道），输出：
  - transcript.json  逐条转写与待办
  - todos.md         可勾选的待办列表

后端可组合，全部离线也能跑：
  转写：--transcriber whisper|openai|mock
  抽取：--extractor rules|llm|mock

只依赖标准库；whisper / openai 仅在显式选用时才 import。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
import tempfile
import wave
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable, List, Optional, Sequence

WAV_RATE = 16000


@dataclass
class Memo:
    path: Path
    duration_ms: int
    text: str = ""


class WavError(RuntimeError):
    pass


def read_wav_info(path: Path) -> int:
    """返回毫秒时长；与板端 tf_library 的格式判定保持一致。"""
    with path.open("rb") as handle:
        header = handle.read(44)
    if len(header) < 44:
        raise WavError(f"{path.name}: 文件过短")
    if header[0:4] != b"RIFF" or header[8:12] != b"WAVE" or header[12:16] != b"fmt ":
        raise WavError(f"{path.name}: 不是 RIFF/WAVE")
    if struct.unpack_from("<I", header, 16)[0] != 16:
        raise WavError(f"{path.name}: fmt 块长度不是 16")
    tag, channels, rate = struct.unpack_from("<HHI", header, 20)
    bits = struct.unpack_from("<H", header, 34)[0]
    if (tag, channels, rate, bits) != (1, 1, WAV_RATE, 16):
        raise WavError(
            f"{path.name}: 期望 16kHz/16bit/单声道，实际 {rate}Hz/{bits}bit/{channels}ch"
        )
    if header[36:40] != b"data":
        raise WavError(f"{path.name}: 缺少 data 块")
    data_bytes = struct.unpack_from("<I", header, 40)[0]
    return int(data_bytes / (WAV_RATE * 2) * 1000)


def collect_memos(target: Path) -> List[Memo]:
    files = [target] if target.is_file() else sorted(target.glob("memo*.wav"))
    memos: List[Memo] = []
    for path in files:
        try:
            memos.append(Memo(path=path, duration_ms=read_wav_info(path)))
        except WavError as exc:
            print(f"[skip] {exc}", file=sys.stderr)
    return memos


def transcribe(memo: Memo, backend: str) -> str:
    if backend == "mock":
        sidecar = memo.path.with_suffix(".txt")
        if sidecar.exists():
            # Windows 记事本/PowerShell 写出的 UTF-8 常带 BOM，按 utf-8-sig 读掉。
            return sidecar.read_text(encoding="utf-8-sig").strip()
        return f"[mock] {memo.path.name} {memo.duration_ms // 1000}s"

    if backend == "whisper":
        try:
            from faster_whisper import WhisperModel  # type: ignore
        except ImportError as exc:
            raise SystemExit(
                "未安装 faster-whisper；pip install faster-whisper 或改用 --transcriber mock"
            ) from exc
        model = WhisperModel(
            os.environ.get("WHISPER_MODEL", "small"),
            device=os.environ.get("WHISPER_DEVICE", "cpu"),
            compute_type=os.environ.get("WHISPER_COMPUTE", "int8"),
        )
        segments, _ = model.transcribe(
            str(memo.path),
            language=os.environ.get("WHISPER_LANG", "zh"),
            initial_prompt=os.environ.get(
                "WHISPER_PROMPT", "以下是普通话录音，请用简体中文输出，保留数字和标点。"
            ),
        )
        return "".join(seg.text for seg in segments).strip()

    if backend == "openai":
        try:
            from openai import OpenAI  # type: ignore
        except ImportError as exc:
            raise SystemExit("未安装 openai；pip install openai") from exc
        client = OpenAI()
        with memo.path.open("rb") as audio:
            result = client.audio.transcriptions.create(
                model="whisper-1", file=audio, language="zh"
            )
        return (result.text or "").strip()

    raise SystemExit(f"未知转写后端：{backend}")


TRIGGERS = (
    "要", "得", "需要", "记得", "别忘了", "待办", "安排", "准备",
    "提醒", "明天", "后天", "下周", "回头", "帮我",
    "todo", "need to", "should", "must", "remember", "remind", "don't forget",
)
NOISE = re.compile(r"^(然后|那个|就是|嗯|啊|呃|这个|所以说)[，,、\s]*")


def split_sentences(text: str) -> Iterable[str]:
    for chunk in re.split(r"[。！？!?;；\n]+", text):
        chunk = chunk.strip()
        if chunk:
            yield chunk


def extract_rules(text: str) -> List[str]:
    """离线抽取：命中触发词且不是纯语气词的句子，去重保序。"""
    todos: List[str] = []
    for sentence in split_sentences(text):
        if not any(trigger in sentence.lower() for trigger in TRIGGERS):
            continue
        candidate = NOISE.sub("", sentence).strip(" ，,、")
        if len(candidate) < 2 or candidate in todos:
            continue
        todos.append(candidate)
    return todos


def extract_llm(text: str) -> List[str]:
    try:
        from openai import OpenAI  # type: ignore
    except ImportError as exc:
        raise SystemExit("未安装 openai；pip install openai 或改用 --extractor rules") from exc
    client = OpenAI()
    prompt = (
        "把下面的语音备忘整理成待办清单，每条一行，不要编号、不要解释；"
        "没有明确行动项就返回空。\n\n" + text
    )
    result = client.chat.completions.create(
        model=os.environ.get("TODO_MODEL", "gpt-4o-mini"),
        messages=[{"role": "user", "content": prompt}],
        temperature=0,
    )
    content = result.choices[0].message.content or ""
    return [line.strip(" -•\t") for line in content.splitlines() if line.strip(" -•\t")]


def extract(text: str, backend: str) -> List[str]:
    if backend == "rules":
        return extract_rules(text)
    if backend == "llm":
        return extract_llm(text)
    if backend == "mock":
        return [f"[mock-todo] {text[:24]}"] if text else []
    raise SystemExit(f"未知抽取后端：{backend}")


def write_outputs(memos: Sequence[Memo], todo_map: List[List[str]], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    payload = []
    for memo, todos in zip(memos, todo_map):
        payload.append(
            {
                "file": memo.path.name,
                "duration_ms": memo.duration_ms,
                "text": memo.text,
                "todos": todos,
            }
        )
    (out_dir / "transcript.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    lines = [f"# 语音待办（{datetime.now():%Y-%m-%d %H:%M}）", ""]
    total = 0
    for memo, todos in zip(memos, todo_map):
        stamp = datetime.fromtimestamp(memo.path.stat().st_mtime).strftime("%m-%d %H:%M")
        if not todos:
            lines.append(f"- [ ] （无明确待办）{memo.path.name} · {stamp}")
            continue
        for todo in todos:
            lines.append(f"- [ ] {todo}  ")
            lines.append(
                f"      <!-- {memo.path.name} · {stamp} · {memo.duration_ms // 1000}s -->"
            )
            total += 1
    lines += ["", f"共 {len(memos)} 条录音，{total} 条待办。"]
    (out_dir / "todos.md").write_text("\n".join(lines), encoding="utf-8")


def _write_test_wav(path: Path, seconds: float = 1.0) -> None:
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(WAV_RATE)
        handle.writeframes(
            b"".join(struct.pack("<h", (i % 100) * 100) for i in range(int(WAV_RATE * seconds)))
        )


def selftest() -> int:
    checks = failures = 0

    def check(cond: bool, what: str) -> None:
        nonlocal checks, failures
        checks += 1
        if not cond:
            failures += 1
            print(f"FAIL {what}", file=sys.stderr)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        wav = root / "memo0000.wav"
        _write_test_wav(wav, 1.0)
        check(900 <= read_wav_info(wav) <= 1100, "时长解析")

        wav.with_suffix(".txt").write_text(
            "明天要交软件实践的测试报告。然后记得给老师发邮件！今天天气不错。",
            encoding="utf-8",
        )
        bad = root / "memo0001.wav"
        bad.write_bytes(b"RIFFxxxxWAVEfmt ")
        rejected = False
        try:
            read_wav_info(bad)
        except WavError:
            rejected = True
        check(rejected, "坏文件被拒绝")
        check(len(collect_memos(root)) == 1, "只收集合法 memo*.wav")

        text = transcribe(collect_memos(root)[0], "mock")
        check("明天" in text, "mock 读取同名 .txt")

        wav.with_suffix(".txt").write_text("\ufeff记得带伞", encoding="utf-8")
        check(transcribe(collect_memos(root)[0], "mock") == "记得带伞", "mock 忽略 BOM")
        wav.with_suffix(".txt").write_text(
            "明天要交软件实践的测试报告。然后记得给老师发邮件！今天天气不错。",
            encoding="utf-8",
        )

        todos = extract_rules(text)
        check(len(todos) == 2, f"规则抽取 2 条，实际 {len(todos)}")
        check(todos[0].startswith("明天要交"), "第一条内容正确")
        check(all("今天天气" not in t for t in todos), "非行动句被过滤")
        check(extract_rules("nothing to do here") == [], "无触发词时为 0")
        check(len(extract_rules("记得带钥匙。记得带钥匙。")) == 1, "重复项去重")

        memos = collect_memos(root)
        write_outputs(memos, [todos], root / "out")
        markdown = (root / "out" / "todos.md").read_text(encoding="utf-8")
        check("- [ ] 明天要交软件实践的测试报告" in markdown, "todos.md 含待办")
        check("memo0000.wav" in markdown, "todos.md 可追溯到录音")
        data = json.loads((root / "out" / "transcript.json").read_text(encoding="utf-8"))
        check(data[0]["todos"] == todos and data[0]["file"] == "memo0000.wav", "json 一致")

    if failures:
        print(f"{failures} failed, {checks} checks", file=sys.stderr)
        return 1
    print(f"ok {checks} checks")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="把板端 WAV 便签转成待办清单（REQ-204）")
    parser.add_argument("--input", help="TF 卡目录（含 memo*.wav）或单个 WAV")
    parser.add_argument("--out", default="todo_out", help="输出目录")
    parser.add_argument("--transcriber", default="mock", choices=["mock", "whisper", "openai"])
    parser.add_argument("--extractor", default="rules", choices=["rules", "llm", "mock"])
    parser.add_argument("--selftest", action="store_true", help="离线自检，不读写板端文件")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.input:
        parser.error("需要 --input，或使用 --selftest")

    target = Path(args.input)
    if not target.exists():
        raise SystemExit(f"输入不存在：{target}")

    memos = collect_memos(target)
    if not memos:
        raise SystemExit("没有找到合法录音（16kHz/16bit/单声道 memo*.wav）")

    todo_map: List[List[str]] = []
    for memo in memos:
        memo.text = transcribe(memo, args.transcriber)
        todos = extract(memo.text, args.extractor)
        todo_map.append(todos)
        print(f"{memo.path.name}: {memo.duration_ms // 1000}s -> {len(todos)} 条待办")

    write_outputs(memos, todo_map, Path(args.out))
    print(f"输出：{Path(args.out) / 'todos.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())



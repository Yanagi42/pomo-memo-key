"""把 memo*.wav 转成同名 .txt。用本机 faster-whisper，不占用用户的总结密钥。"""

from __future__ import annotations

import wave
from pathlib import Path
from typing import Callable

import numpy as np

_model = None
_to_simplified = None


def to_simplified(text: str) -> str:
    """Whisper 的中文结果经常是繁体，统一转成简体。"""
    global _to_simplified
    if not text:
        return text
    if _to_simplified is None:
        from opencc import OpenCC
        _to_simplified = OpenCC("t2s")
    return _to_simplified.convert(text)


def load_audio(path: Path) -> np.ndarray:
    """读板端 16 kHz 单声道 PCM，绕开当前 PyAV 不接受的 metadata_errors。"""
    with wave.open(str(path), "rb") as handle:
        if handle.getnchannels() != 1 or handle.getsampwidth() != 2 or handle.getframerate() != 16000:
            raise RuntimeError(f"{path.name} 不是 16 kHz、16 bit、单声道")
        pcm = np.frombuffer(handle.readframes(handle.getnframes()), dtype=np.int16)
    return np.array(pcm, dtype=np.float32) / 32768.0


def transcribe_folder(folder: Path, progress: Callable[[str], None]) -> None:
    global _model
    wavs = sorted(folder.glob("memo*.wav"))
    pending = [path for path in wavs if _needs_text(path)]
    if not pending:
        return
    try:
        from faster_whisper import WhisperModel
    except ImportError as exc:
        raise RuntimeError("没有安装 faster-whisper。请运行：python -m pip install faster-whisper") from exc
    if _model is None:
        progress("正在准备转写模型，第一次会下载…")
        _model = WhisperModel("small", device="cpu", compute_type="int8")
    for index, path in enumerate(pending, 1):
        progress(f"正在转写 {index}/{len(pending)} {path.name}")
        segments, _info = _model.transcribe(
            load_audio(path),
            language="zh",
            vad_filter=True,
            condition_on_previous_text=False,
        )
        parts = [segment.text.strip() for segment in segments if segment.no_speech_prob < 0.6 and segment.text.strip()]
        text = to_simplified("".join(parts).strip())
        path.with_suffix(".txt").write_text(text + ("\n" if text else ""), encoding="utf-8")


def _needs_text(path: Path) -> bool:
    sidecar = path.with_suffix(".txt")
    if not sidecar.is_file():
        return True
    if sidecar.stat().st_mtime < path.stat().st_mtime:
        return True
    # 上一版把提示词写进了静音录音，这种结果要重转。
    body = sidecar.read_text(encoding="utf-8-sig").strip()
    return "请用简体中文输出" in body

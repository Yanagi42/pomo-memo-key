"""闪念总结：读一份从设备拷来的录音目录，汇总番茄钟记录和录音。

用法：
    python desktop/summary_app/app.py
    python desktop/summary_app/app.py --selftest

「读取设备」会连已经配对的 POMO_MEMO_HID，把录音和 pomo.log 拉下来，再在本机转写。
总结由用户自带密钥：填了密钥、接口地址和模型名称才请求接口，否则只做本地汇总。
录音是 memo*.wav（16 kHz、16 bit、单声道）。同名 .txt 当作转写文本。
番茄钟记录是同目录的 pomo.log，每行：日期 时间 分钟 结果
    2026-11-12 10:42 25 done
结果用 done 或 stopped。当前固件会把新的计时写到卡上的这个文件。
"""

from __future__ import annotations

import json
import queue
import struct
import sys
import threading
import tkinter as tk
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

WAV_RATE = 16000


class RecordError(RuntimeError):
    pass


@dataclass
class Memo:
    path: Path
    duration_ms: int
    text: str


@dataclass
class Pomo:
    when: datetime
    minutes: int
    result: str


def read_wav_ms(path: Path) -> int:
    with path.open("rb") as handle:
        header = handle.read(44)
    if len(header) < 44 or header[0:4] != b"RIFF" or header[8:12] != b"WAVE":
        raise RecordError(f"{path.name} 不是 WAV")
    if header[12:16] != b"fmt " or struct.unpack_from("<I", header, 16)[0] != 16:
        raise RecordError(f"{path.name} 的 fmt 块不符合板端格式")
    tag, channels, rate = struct.unpack_from("<HHI", header, 20)
    bits = struct.unpack_from("<H", header, 34)[0]
    if (tag, channels, rate, bits) != (1, 1, WAV_RATE, 16) or header[36:40] != b"data":
        raise RecordError(f"{path.name} 不是 16 kHz、16 bit、单声道")
    data_bytes = struct.unpack_from("<I", header, 40)[0]
    return int(data_bytes / (WAV_RATE * 2) * 1000)


def load_memos(folder: Path) -> tuple[list[Memo], list[str]]:
    memos: list[Memo] = []
    skipped: list[str] = []
    for path in sorted(folder.glob("memo*.wav")):
        try:
            duration_ms = read_wav_ms(path)
        except RecordError as exc:
            skipped.append(str(exc))
            continue
        sidecar = path.with_suffix(".txt")
        if sidecar.is_file():
            from transcribe import to_simplified
            text = to_simplified(sidecar.read_text(encoding="utf-8-sig").strip())
        else:
            text = None
        memos.append(Memo(path, duration_ms, text or ""))
    return memos, skipped


def load_pomos(folder: Path) -> list[Pomo]:
    path = folder / "pomo.log"
    if not path.is_file():
        return []
    rows: list[Pomo] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 4:
            raise RecordError(f"pomo.log 第 {line_no} 行应为：日期 时间 分钟 done|stopped")
        when = datetime.strptime(f"{parts[0]} {parts[1]}", "%Y-%m-%d %H:%M")
        minutes = int(parts[2])
        result = parts[3]
        if result not in ("done", "stopped") or minutes <= 0:
            raise RecordError(f"pomo.log 第 {line_no} 行的分钟或结果无法识别")
        rows.append(Pomo(when, minutes, result))
    return rows


def _minutes_text(total_ms: int) -> str:
    if 0 < total_ms < 1000:
        return "不到 1 秒"
    seconds = total_ms // 1000
    return f"{seconds // 60} 分 {seconds % 60} 秒"


def local_summary(memos: list[Memo], pomos: list[Pomo], skipped: list[str]) -> str:
    lines = ["番茄钟", ""]
    if not pomos:
        lines.append("这个目录里没有 pomo.log。计时要在本次烧录之后走完，或在计时中按刷新停下，才会追加到卡上。这次没有读到这份记录。")
    else:
        done = [row for row in pomos if row.result == "done"]
        stopped = [row for row in pomos if row.result == "stopped"]
        done_min = sum(row.minutes for row in done)
        lines.append(f"共 {len(pomos)} 条，完成 {len(done)} 次（{done_min} 分钟），中途停下 {len(stopped)} 次。")
        for row in pomos:
            label = "完成" if row.result == "done" else "停下"
            lines.append(f"- {row.when:%Y-%m-%d %H:%M}  {row.minutes} 分钟  {label}")

    lines.extend(["", "录音", ""])
    if not memos:
        lines.append("没有找到 memo*.wav。")
    else:
        total = sum(item.duration_ms for item in memos)
        spoken = [item for item in memos if item.text and "请用简体中文输出" not in item.text]
        quiet = len(memos) - len(spoken)
        lines.append(f"共 {len(memos)} 条，合计 {_minutes_text(total)}。其中 {len(spoken)} 条辨认出语音，{quiet} 条没有可辨认的语音。")
        for item in memos:
            if item.text and "请用简体中文输出" not in item.text:
                head = item.text.splitlines()[0]
            elif item.text:
                head = "没有可辨认的语音"
            else:
                head = "没有可辨认的语音" if (item.path.with_suffix(".txt")).is_file() else "尚未转写"
            lines.append(f"- {item.path.name}  {_minutes_text(item.duration_ms)}  {head}")
    if skipped:
        lines.extend(["", "跳过"])
        lines.extend(f"- {item}" for item in skipped)
    return "\n".join(lines)


def remote_summary(memos: list[Memo], pomos: list[Pomo], key: str, base_url: str, model: str) -> str:
    if not key or not base_url or not model:
        raise RecordError("密钥、接口地址和模型名称需要一起填写，已改用本地汇总。")
    endpoint = base_url.rstrip("/")
    if not endpoint.endswith("/chat/completions"):
        endpoint += "/chat/completions"
    payload = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "你在整理一块桌面设备的番茄钟和语音备忘。"
                    "只用给定记录写一段简短中文总结：完成了几次、一共多久、录音里说到的事。"
                    "记录里没有的内容不要编。"
                ),
            },
            {"role": "user", "content": local_summary(memos, pomos, [])},
        ],
    }
    request = urllib.request.Request(
        endpoint,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            body = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:300]
        raise RecordError(f"总结请求失败：HTTP {exc.code} {detail}") from exc
    except urllib.error.URLError as exc:
        raise RecordError(f"总结请求失败：{exc.reason}") from exc
    try:
        return body["choices"][0]["message"]["content"].strip()
    except (KeyError, IndexError, TypeError) as exc:
        raise RecordError("总结接口没有返回正文。") from exc


def summarize(folder: Path, key: str = "", base_url: str = "", model: str = "") -> str:
    memos, skipped = load_memos(folder)
    pomos = load_pomos(folder)
    if key.strip() and (memos or pomos):
        try:
            text = remote_summary(memos, pomos, key.strip(), base_url.strip(), model.strip())
        except RecordError as exc:
            text = local_summary(memos, pomos, skipped) + f"\n\n{exc}"
        else:
            if skipped:
                text += "\n\n跳过\n" + "\n".join(f"- {item}" for item in skipped)
        return text
    return local_summary(memos, pomos, skipped)


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("闪念总结")
        self.geometry("760x560")
        self.folder = tk.StringVar()
        self.api_key = tk.StringVar()
        self.api_base = tk.StringVar()
        self.api_model = tk.StringVar()
        self.results: queue.Queue[tuple[str, str]] = queue.Queue()
        self._build()
        self.after(100, self._drain)

    def _build(self) -> None:
        bar = ttk.Frame(self, padding=12)
        bar.pack(fill="x")
        ttk.Entry(bar, textvariable=self.folder).pack(side="left", fill="x", expand=True)
        ttk.Button(bar, text="读取设备", command=self._pull_device).pack(side="left", padx=(8, 0))
        ttk.Button(bar, text="选择目录", command=self._choose).pack(side="left", padx=(8, 0))
        ttk.Button(bar, text="总结", command=self._start).pack(side="left", padx=(8, 0))
        byok = ttk.Frame(self, padding=(12, 0, 12, 0))
        byok.pack(fill="x")
        ttk.Label(byok, text="密钥").pack(side="left")
        ttk.Entry(byok, textvariable=self.api_key, show="*", width=18).pack(side="left", padx=(4, 8))
        ttk.Label(byok, text="接口").pack(side="left")
        ttk.Entry(byok, textvariable=self.api_base, width=28).pack(side="left", padx=(4, 8))
        ttk.Label(byok, text="模型").pack(side="left")
        ttk.Entry(byok, textvariable=self.api_model, width=16).pack(side="left", padx=(4, 0))
        self.status = tk.StringVar(value="点「读取设备」。板子需已在 Windows 蓝牙里连上。密钥留空则只做本地汇总。")
        ttk.Label(self, textvariable=self.status, padding=(12, 0)).pack(fill="x")
        self.output = tk.Text(self, wrap="word", padx=12, pady=12)
        self.output.pack(fill="both", expand=True, padx=12, pady=12)

    def _choose(self) -> None:
        selected = filedialog.askdirectory()
        if selected:
            self.folder.set(selected)

    def _pull_device(self) -> None:
        self.status.set("正在读取已连接的设备…")
        self.output.delete("1.0", "end")
        threading.Thread(target=self._pull_work, args=(self._byok(),), daemon=True).start()

    def _byok(self) -> tuple[str, str, str]:
        return self.api_key.get().strip(), self.api_base.get().strip(), self.api_model.get().strip()

    def _pull_work(self, byok: tuple[str, str, str]) -> None:
        from ble_pull import PullError, pull_connected

        dest = Path(__file__).resolve().parent / "from_device"

        def progress(message: str) -> None:
            self.results.put(("status", message))

        try:
            from transcribe import transcribe_folder

            count = pull_connected(dest, progress)
            transcribe_folder(dest, progress)
            text = summarize(dest, *byok)
        except (PullError, RecordError, RuntimeError) as exc:
            self.results.put(("error", str(exc)))
            return
        except Exception as exc:
            self.results.put(("error", f"读取失败：{exc}"))
            return
        self.results.put(("folder", str(dest)))
        self.results.put(("ok", text))
        self.results.put(("status", f"已读取 {count} 个文件，并完成转写。"))

    def _start(self) -> None:
        folder = Path(self.folder.get().strip())
        if not folder.is_dir():
            messagebox.showerror("闪念总结", "请先选择一个目录。")
            return
        self.status.set("正在读取…")
        self.output.delete("1.0", "end")
        threading.Thread(target=self._work, args=(folder, self._byok()), daemon=True).start()

    def _work(self, folder: Path, byok: tuple[str, str, str]) -> None:
        try:
            text = summarize(folder, *byok)
        except RecordError as exc:
            self.results.put(("error", str(exc)))
        except Exception as exc:  # 界面线程之外，把意外显示出来
            self.results.put(("error", f"读取失败：{exc}"))
        else:
            self.results.put(("ok", text))

    def _drain(self) -> None:
        while True:
            try:
                kind, text = self.results.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                self.status.set(text)
            elif kind == "folder":
                self.folder.set(text)
            else:
                self.output.delete("1.0", "end")
                self.output.insert("1.0", text)
                self.status.set("没有完成" if kind == "error" else "完成")
        self.after(100, self._drain)


def selftest() -> int:
    import tempfile

    failed = 0
    with tempfile.TemporaryDirectory() as raw:
        folder = Path(raw)
        wav = folder / "memo0001.wav"
        header = struct.pack(
            "<4sI4s4sIHHIIHH4sI",
            b"RIFF", 36 + 32000, b"WAVE", b"fmt ", 16, 1, 1, 16000, 32000, 2, 16, b"data", 32000,
        )
        wav.write_bytes(header + b"\x00" * 32000)
        (wav.with_suffix(".txt")).write_text("明天要交报告\n", encoding="utf-8")
        (folder / "pomo.log").write_text("2026-11-12 10:42 25 done\n2026-11-12 14:00 10 stopped\n", encoding="utf-8")
        memos, skipped = load_memos(folder)
        pomos = load_pomos(folder)
        if len(memos) != 1 or memos[0].duration_ms != 1000 or memos[0].text != "明天要交报告":
            failed += 1
        if skipped or len(pomos) != 2 or pomos[0].minutes != 25 or pomos[1].result != "stopped":
            failed += 1
        text = local_summary(memos, pomos, skipped)
        if "完成 1 次" not in text or "memo0001.wav" not in text or "明天要交报告" not in text:
            failed += 1
        from ble_pull import selftest as ble_selftest
        failed += ble_selftest()
        empty = local_summary([], [], [])
        if "没有 pomo.log" not in empty:
            failed += 1
    print("ok" if failed == 0 else f"failed {failed}")
    return failed


def main() -> int:
    if "--selftest" in sys.argv:
        return selftest()
    App().mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""从已经配对的 POMO_MEMO_HID 拉取 memoNNNN.wav。

优先用 Windows 里处于已连接状态的那块板。没有已连接时，再用已配对的同一名字。
"""

from __future__ import annotations

import asyncio
import re
from pathlib import Path
from typing import Callable, Optional

NAME_RE = re.compile(r"^memo\d{4}\.wav$")
LOG_NAME = "pomo.log"
SERVICE_HINT = "c0de0001"
BOARD_NAME = "POMO_MEMO_HID"


class PullError(RuntimeError):
    pass


def parse_files(lines: list[str]) -> list[tuple[str, int]]:
    files: list[tuple[str, int]] = []
    for line in lines:
        line = line.strip()
        if line == "OK":
            return files
        if line.startswith("X "):
            raise PullError(_board_error(line[2:].strip()))
        parts = line.split()
        if len(parts) == 3 and parts[0] == "F" and (NAME_RE.match(parts[1]) or parts[1] == LOG_NAME):
            files.append((parts[1], int(parts[2])))
    raise PullError("设备没有把录音列表发完。")


class FileAssembler:
    def __init__(self, expect_name: str) -> None:
        self.expect_name = expect_name
        self.buf = bytearray()
        self.declared: Optional[int] = None

    def feed(self, raw: str) -> Optional[bytes]:
        line = raw.strip()
        if line.startswith("X "):
            raise PullError(_board_error(line[2:].strip()))
        if line.startswith("B "):
            parts = line.split()
            if len(parts) != 3 or parts[1] != self.expect_name:
                raise PullError("设备传来的文件名对不上。")
            self.declared = int(parts[2])
            self.buf.clear()
            return None
        if line.startswith("D "):
            if self.declared is None:
                raise PullError("录音数据不完整。")
            self.buf += bytes.fromhex(line[2:].strip())
            return None
        if line == "E":
            if self.declared is None or len(self.buf) != self.declared:
                raise PullError(f"录音没收全：收到 {len(self.buf)} 字节，设备说有 {self.declared} 字节。")
            return bytes(self.buf)
        return None


def _board_error(code: str) -> str:
    if code == "9":
        return "设备正在录音或播放，等它停下来再读。"
    if code == "1":
        return "设备上没有这条录音。"
    return f"设备拒绝了读取（错误 {code}）。"


async def _lines(buffer: bytearray, event: asyncio.Event, timeout: float = 30.0):
    while True:
        while b"\n" in buffer:
            raw, _, rest = buffer.partition(b"\n")
            buffer[:] = rest
            text = raw.decode("ascii", "replace").strip()
            if text:
                yield text
        event.clear()
        try:
            await asyncio.wait_for(event.wait(), timeout)
        except asyncio.TimeoutError as exc:
            raise PullError("设备没有继续传数据。确认它还连着，并且没有正在录音。") from exc


async def find_connected_board():
    from bleak import BleakAdapter

    adapter = await BleakAdapter.get()
    devices = await adapter.get_connected_devices(["1801"])
    named = [item for item in devices if "POMO_MEMO" in (item.name or "").upper()]
    if not named:
        seen = "、".join(item.name or item.address for item in devices) or "无"
        raise PullError(f"没有已连接的 POMO_MEMO_HID。当前连着的蓝牙设备是：{seen}。")
    return named[0]


async def pull_memos(out_dir: Path, progress: Callable[[str], None]) -> int:
    try:
        from bleak import BleakClient
    except ImportError as exc:
        raise PullError("缺少 bleak。请先运行：python -m pip install bleak") from exc

    board = await find_connected_board()
    progress(f"正在连接 {board.name}…")
    out_dir.mkdir(parents=True, exist_ok=True)
    client = BleakClient(board)
    await client.connect()
    try:
        write_char = notify_char = None
        for service in client.services:
            if SERVICE_HINT not in service.uuid.replace("-", "").lower():
                continue
            for char in service.characteristics:
                props = {item.lower() for item in char.properties}
                if write_char is None and ("write" in props or "write-without-response" in props):
                    write_char = char
                if notify_char is None and "notify" in props:
                    notify_char = char
        if write_char is None or notify_char is None:
            raise PullError("连上了设备，但没有找到录音传输服务。")

        buffer = bytearray()
        event = asyncio.Event()

        def on_data(_sender, data: bytearray) -> None:
            buffer.extend(data)
            event.set()

        await client.start_notify(notify_char, on_data)
        try:
            progress("正在读取录音列表…")
            await client.write_gatt_char(write_char, b"L\n", response=False)
            listing: list[str] = []
            async for line in _lines(buffer, event):
                listing.append(line)
                if line == "OK" or line.startswith("X "):
                    break
            files = parse_files(listing)
            if not files:
                progress("设备上没有录音。")
                return 0
            for index, (file_name, _size) in enumerate(files, 1):
                progress(f"正在读取 {index}/{len(files)} {file_name}")
                if not (NAME_RE.match(file_name) or file_name == LOG_NAME):
                    raise PullError(f"设备给出了不认识的文件名 {file_name}。")
                await client.write_gatt_char(write_char, f"G {file_name}\n".encode("ascii"), response=False)
                assembler = FileAssembler(file_name)
                saved = None
                async for line in _lines(buffer, event):
                    saved = assembler.feed(line)
                    if saved is not None:
                        break
                if saved is None:
                    raise PullError(f"{file_name} 没有传完。")
                (out_dir / file_name).write_bytes(saved)
        finally:
            await client.stop_notify(notify_char)
    finally:
        await client.disconnect()
    return len(files)


def pull_connected(out_dir: Path, progress: Callable[[str], None]) -> int:
    try:
        from bleak.backends.winrt.util import allow_sta
        allow_sta()
    except Exception:
        pass
    return asyncio.run(pull_memos(out_dir, progress))


def selftest() -> int:
    files = parse_files(["F memo0001.wav 44", "F pomo.log 20", "OK"])
    ok = files == [("memo0001.wav", 44), ("pomo.log", 20)]
    assembler = FileAssembler("memo0001.wav")
    payload = b"abc"
    assert assembler.feed(f"B memo0001.wav {len(payload)}") is None
    assert assembler.feed("D " + payload.hex()) is None
    got = assembler.feed("E")
    ok = ok and got == payload
    return 0 if ok else 1

#!/usr/bin/env python3
"""REQ-203：通过 BLE 私有 GATT 服务把板上的 memoNNNN.wav 拉到电脑。

协议见 docs/11-REQ-203-BLE文件传输-设计与协议.md
  主机 -> 板 : V / L / G <name>
  板 -> 主机 : V1 <n> / F <name> <size> ... OK / B <name> <size> / D <hex> / E / X <code>

--selftest 只验证协议编解码，不需要 bleak，也不需要板子。
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import random
import re
import sys
import time
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple

NAME_RE = re.compile(r"^memo\d{4}\.wav$")
# 板端自建服务的 128-bit UUID：c0de0001-5f1f-4b1e-9c3a-706f6d6f0001（见 docs/12）
SERVICE_HINT = "c0de0001"


def cmd_version() -> bytes:
    return b"V\n"


def cmd_list() -> bytes:
    return b"L\n"


def cmd_get(name: str) -> bytes:
    if not NAME_RE.match(name):
        raise ValueError(f"非法文件名：{name}")
    return f"G {name}\n".encode("ascii")


def parse_files(lines: Iterable[str]) -> List[Tuple[str, int]]:
    """解析 F 行，遇到 OK 结束。"""
    files: List[Tuple[str, int]] = []
    for line in lines:
        line = line.strip()
        if line == "OK":
            return files
        if line.startswith("X "):
            raise RuntimeError(f"板端错误码 {line[2:]}")
        if not line.startswith("F "):
            continue
        parts = line.split()
        if len(parts) != 3 or not NAME_RE.match(parts[1]):
            continue
        files.append((parts[1], int(parts[2])))
    raise RuntimeError("列表未以 OK 结束")


class FileAssembler:
    """按行喂入 B/D/E 帧，收齐后返回原始字节（同步/异步两条路径共用）。"""

    def __init__(self, expect_name: str) -> None:
        self.expect_name = expect_name
        self.buf = bytearray()
        self.declared: Optional[int] = None

    def feed(self, raw: str) -> Optional[bytes]:
        line = raw.strip()
        if line.startswith("X "):
            raise RuntimeError(f"板端错误码 {line[2:]}")
        if line.startswith("B "):
            parts = line.split()
            if len(parts) != 3:
                raise RuntimeError("B 帧格式错")
            name, size = parts[1], int(parts[2])
            if name != self.expect_name:
                raise RuntimeError(f"文件名不符：{name}")
            self.declared = size
            self.buf.clear()
            return None
        if line.startswith("D "):
            if self.declared is None:
                raise RuntimeError("数据早于 B 帧")
            self.buf += bytes.fromhex(line[2:].strip())
            return None
        if line == "E":
            if self.declared is None or len(self.buf) != self.declared:
                raise RuntimeError(f"尺寸不符：收到 {len(self.buf)}，声明 {self.declared}")
            return bytes(self.buf)
        return None


def parse_file(lines: Iterable[str], expect_name: str) -> bytes:
    """解析 B/D/E 帧；名字不符或尺寸不符直接报错。"""
    assembler = FileAssembler(expect_name)
    for line in lines:
        data = assembler.feed(line)
        if data is not None:
            return data
    raise RuntimeError("文件未正常结束")


async def read_files(channel: "Channel") -> List[Tuple[str, int]]:
    """读 F… 行直到 OK（失败抛 RuntimeError）。"""
    lines: List[str] = []
    async for line in channel.lines():
        lines.append(line)
        if line == "OK" or line.startswith("X "):
            return parse_files(lines)
    raise RuntimeError("列表未以 OK 结束")


async def read_file(channel: "Channel", name: str) -> bytes:
    """读 B/D/E 帧直到 E（失败抛 RuntimeError）。"""
    assembler = FileAssembler(name)
    async for line in channel.lines():
        data = assembler.feed(line)
        if data is not None:
            return data
    raise RuntimeError("文件未正常结束")


def selftest() -> int:
    checks = failures = 0

    def check(cond: bool, what: str) -> None:
        nonlocal checks, failures
        checks += 1
        if not cond:
            failures += 1
            print(f"FAIL {what}", file=sys.stderr)

    check(cmd_get("memo0003.wav") == b"G memo0003.wav\n", "GET 组帧")
    rejected = False
    try:
        cmd_get("../etc/passwd")
    except ValueError:
        rejected = True
    check(rejected, "拒绝路径穿越")

    files = parse_files(["F memo0000.wav 960044", "F memo0001.wav 120", "OK"])
    check(files == [("memo0000.wav", 960044), ("memo0001.wav", 120)], "列表解析")

    payload = bytes(range(64))
    frames = ["B memo0000.wav 64", "D " + payload[:32].hex(), "D " + payload[32:].hex(), "E"]
    check(parse_file(frames, "memo0000.wav") == payload, "文件重组")

    short = ["B memo0000.wav 64", "D " + payload[:32].hex(), "E"]
    ok = False
    try:
        parse_file(short, "memo0000.wav")
    except RuntimeError:
        ok = True
    check(ok, "尺寸不足被拒绝")

    ok = False
    try:
        parse_file(["B memo0000.wav 1", "X 2"], "memo0000.wav")
    except RuntimeError:
        ok = True
    check(ok, "错误码透传")

    check(len(hashlib.sha256(payload).hexdigest()) == 64, "sha256 可算（回执校验用）")

    if failures:
        print(f"{failures} failed, {checks} checks", file=sys.stderr)
        return 1
    print(f"ok {checks} checks")
    return 0


class Channel:
    """把 notify 回调收集成可 await 的行流。"""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.event = asyncio.Event()

    def on_data(self, _sender, data: bytearray) -> None:
        self.buffer += bytes(data)
        self.event.set()

    async def lines(self, timeout: float = 20.0):
        while True:
            while b"\n" in self.buffer:
                raw, _, rest = self.buffer.partition(b"\n")
                self.buffer = bytearray(rest)
                text = raw.decode("ascii", "replace").strip()
                if text:
                    yield text
            self.event.clear()
            try:
                await asyncio.wait_for(self.event.wait(), timeout)
            except asyncio.TimeoutError:
                raise SystemExit("等待板端数据超时（检查是否已连接/固件是否含 ble_file）")


def board_frames(payload: bytes, name: str, mtu: int) -> List[str]:
    """模拟板端切帧：chunk = clamp((mtu-7)//2, 8, 192)，一条 D 帧一行。"""
    chunk = max(8, min(192, (mtu - 7) // 2))
    frames = [f"B {name} {len(payload)}"]
    frames += ["D " + payload[i:i + chunk].hex() for i in range(0, len(payload), chunk)]
    frames.append("E")
    return frames


def _wav_payload(size: int) -> bytes:
    """构造形如 30 s 录音的负载（合法 WAV 头 + 伪随机 PCM），用于 SHA256 比对。"""
    header = bytearray(44)
    header[0:4] = b"RIFF"
    header[4:8] = (size - 8).to_bytes(4, "little")
    header[8:12] = b"WAVE"
    header[12:16] = b"fmt "
    header[16:20] = (16).to_bytes(4, "little")
    header[20:22] = (1).to_bytes(2, "little")
    header[22:24] = (1).to_bytes(2, "little")
    header[24:28] = (16000).to_bytes(4, "little")
    header[28:32] = (32000).to_bytes(4, "little")
    header[32:34] = (2).to_bytes(2, "little")
    header[34:36] = (16).to_bytes(2, "little")
    header[36:40] = b"data"
    header[40:44] = (size - 44).to_bytes(4, "little")
    return bytes(header) + random.Random(1101).randbytes(max(0, size - 44))


def _feed(channel: "Channel", frames: Sequence[str], mtu: int, seed: int) -> None:
    """把帧流切成随机 BLE 写包（1..mtu-3 字节）灌进去，模拟真实分片。"""
    limit = max(1, mtu - 3)
    rnd = random.Random(seed)
    for frame in frames:
        data = (frame + "\n").encode("ascii")
        i = 0
        while i < len(data):
            n = rnd.randint(1, limit)
            channel.on_data(None, bytearray(data[i:i + n]))
            i += n


async def _pull_case(frames: Sequence[str], name: str, mtu: int) -> bytes:
    channel = Channel()
    _feed(channel, frames, mtu, mtu)
    return await read_file(channel, name)


async def _list_case(frames: Sequence[str], mtu: int) -> List[Tuple[str, int]]:
    channel = Channel()
    _feed(channel, frames, mtu, mtu)
    return await read_files(channel)


async def _error_case(frames: Sequence[str], name: str, mtu: int) -> bool:
    channel = Channel()
    _feed(channel, frames, mtu, mtu)
    try:
        await read_file(channel, name)
    except RuntimeError:
        return True
    return False


def offline_e2e(size: int, mtus: Sequence[int]) -> int:
    """用“板端帧模型 + 随机分片”走完 sync.py 的真实收包路径，比对 SHA256。"""
    name = "memo0000.wav"
    payload = _wav_payload(size)
    want = hashlib.sha256(payload).hexdigest()
    ok = True
    print(f"离线端到端：{name} {size} B（sha256 {want[:12]}），MTU {list(mtus)}")
    for mtu in mtus:
        frames = board_frames(payload, name, mtu)
        chunk = max(8, min(192, (mtu - 7) // 2))
        started = time.perf_counter()
        got = asyncio.run(_pull_case(frames, name, mtu))
        elapsed = time.perf_counter() - started
        same = len(got) == len(payload) and hashlib.sha256(got).hexdigest() == want
        ok = ok and same
        print(f"  mtu={mtu:<4} chunk={chunk:<3} frames={len(frames):<6} "
              f"{'SHA256 一致' if same else 'SHA256 不符'}  主机处理 {elapsed * 1000:.0f} ms")

        listed = asyncio.run(_list_case([f"F {name} {len(payload)}", "OK"], mtu))
        ok = ok and listed == [(name, len(payload))]

        ok = ok and asyncio.run(_error_case(["X 9"], name, mtu))
    print("ok 离线端到端（列表 / 拉取 / 错误码）" if ok else "FAIL 离线端到端")
    return 0 if ok else 1


async def _resolve_chars(client) -> Tuple[object, object]:
    for service in client.services:
        if SERVICE_HINT in service.uuid.replace("-", "").lower():
            write = notify = None
            for char in service.characteristics:
                props = {p.lower() for p in char.properties}
                if "write" in props or "write-without-response" in props:
                    write = write or char
                if "notify" in props:
                    notify = notify or char
            if write and notify:
                return write, notify
    raise SystemExit("没找到文件传输服务（UUID 前缀 c0de0001）")


async def pull(address: Optional[str], name: str, out_dir: Path, only: Optional[str] = None) -> int:
    from bleak import BleakClient, BleakScanner  # type: ignore

    if address:
        client = BleakClient(address)
    else:
        device = await BleakScanner.find_device_by_name(name, timeout=10.0)
        if not device:
            raise SystemExit(f"没找到名为 {name} 的设备；先配对/靠近，或用 --address 指定")
        client = BleakClient(device)
    await client.connect()

    write_char, notify_char = await _resolve_chars(client)
    channel = Channel()
    await client.start_notify(notify_char, channel.on_data)

    async def send(payload: bytes) -> None:
        await client.write_gatt_char(write_char, payload, response=False)

    await send(cmd_list())
    files = await read_files(channel)
    if only:
        files = [item for item in files if item[0] == only]
        if not files:
            raise SystemExit(f"板上没有 {only}")
    print(f"板上共 {len(files)} 条待拉取录音")

    out_dir.mkdir(parents=True, exist_ok=True)
    for file_name, size in files:
        await send(cmd_get(file_name))
        data = await read_file(channel, file_name)
        (out_dir / file_name).write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()[:12]
        print(f"  {file_name}  {len(data)} B  sha256={digest}")
        if len(data) != size:
            print(f"  ! 与列表尺寸不符（列表 {size}）", file=sys.stderr)

    await client.stop_notify(notify_char)
    await client.disconnect()
    print(f"完成 -> {out_dir}")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="通过 BLE 拉取板上录音（REQ-203）")
    parser.add_argument("--selftest", action="store_true", help="只验证协议编解码")
    parser.add_argument("--selftest-e2e", action="store_true",
                        help="离线端到端：模拟板端帧流 + 随机分片，校验 SHA256（不需要板子/bleak）")
    parser.add_argument("--mtu", type=int, default=247, help="离线端到端使用的 MTU（默认 247）")
    parser.add_argument("--size", type=int, default=960044, help="离线端到端的负载字节数（默认 30 s 录音）")
    parser.add_argument("--scan", action="store_true", help="列出附近 BLE 设备")
    parser.add_argument("--pull", action="store_true", help="连接并拉取全部录音")
    parser.add_argument("--out", default="memos", help="保存目录")
    parser.add_argument("--only", help="只拉取指定文件名（如 memo0012.wav）")
    parser.add_argument("--address", help="直接指定 BLE 地址，跳过按名字扫描")
    parser.add_argument("--name", default="POMO_MEMO_HID", help="广播名")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    if args.selftest_e2e:
        mtus = [args.mtu] if args.mtu in (23, 247, 517) else [23, 247, 517]
        return offline_e2e(args.size, mtus)

    try:
        from bleak import BleakScanner  # type: ignore
    except ImportError:
        raise SystemExit("需要 bleak：pip install bleak（--selftest 不需要）")

    if args.scan:
        async def scan() -> None:
            for dev in await BleakScanner.discover(timeout=8.0):
                print(f"{dev.address}  {dev.name}")
        asyncio.run(scan())
        return 0

    if args.pull:
        return asyncio.run(pull(args.address, args.name, Path(args.out), args.only))

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


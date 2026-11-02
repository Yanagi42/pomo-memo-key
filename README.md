# 闪念胶囊

黄山派上的桌面番茄钟，也是按一下就能录音的便签。需求在 `docs/01-需求规格说明.md`。不依赖板子的逻辑在 `capsule/`，电脑上可以先跑：

```bash
make -C capsule test
```

固件工程在 `project/`，应用入口在 `src/main.c`。它现在会跑 `capsule_selftest`，然后让板载 RGB 灯闪烁。IMU、KEY2、麦克风和界面还没接。

板型是 **sf32lb52-lchspi-ulp**，SDK 为 v2.5.1。在已经执行 SDK export 的终端里：

```bash
cd project
scons --board=sf32lb52-lchspi-ulp -j8
```

2026-11-02 已编出 `main.bin`，还没烧录。喇叭只插标了 **SPK** 的口。板端 B1 到 B9 未执行。

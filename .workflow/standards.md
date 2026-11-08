# Standards

Fill this in at project setup and keep it under one screen. Delete lines that do not apply and replace every placeholder. This file overrides `CONVENTIONS.md` and `STYLE.md`.

## Project

闪念胶囊是黄山派上的桌面番茄钟和按键录音便签。没有网络入口；录音来自板载麦克风并暂存在设备内存。

## Commands

```sh
source /home/shizuku/.sifli/SiFli-SDK/main/export.sh -t gcc
make -C capsule test
cd project && scons --board=sf32lb52-lchspi-ulp -j8
cd project/build_sf32lb52-lchspi-ulp_hcpu && sftool -p /dev/ttyUSB0 -c SF32LB52 -m nor write_flash --verify "bootloader/bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab/ftab.bin@0x12000000"
```

Real path: 在 SDK export 的终端构建并烧录 `project/` 固件；连接 `/dev/ttyUSB0`、1000000 baud 观察启动及功能日志，按 `docs/03-测试计划.md` 在板上执行验收。

## Layout

`src/` 板端应用；`capsule/` 与硬件无关的逻辑及主机测试；`project/` SiFli SDK 工程；`docs/` 需求、设计、测试和风险；`evidence/` 板端日志。

## Code conventions

逻辑状态机留在 `capsule/` 并用主机测试；驱动调用留在 `src/`。板端通过必须有可复现的串口日志或实物记录；不把主机测试写成板端通过。

## Branches and landing

Landing: direct


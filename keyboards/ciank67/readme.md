# ciank67

使用 nRF52840 的 67 键蓝牙键盘，支持 IS31FL3741 RGB Matrix 和 WS2812 RGBLight。

键盘定义位于 `keyboard.json`。`LAYOUT` 由 QMK 根据其中的 `matrix` 坐标生成，沿用原有的 5×14 矩阵及 67 个按键顺序。蓝牙参数和硬件引脚保留在 `config.h` 中。

编译默认键位：

```sh
make ciank67:default
```

需要 `qmk`、Python 3、ARM GCC 工具链及仓库的 nRF5 SDK 子模块。若使用 QMK 安装在用户目录下的工具链，可先执行：

```sh
export PATH="$HOME/.local/share/qmk/bin:$PATH"
```

编译后，BIN 文件位于仓库根目录，HEX 和 UF2 文件位于 `.build/`。使用支持 `pkg generate` 和 `dfu usb-serial` 的 Nordic `nrfutil` 及相应 DFU 引导程序刷写：

```sh
make ciank67:default:nrfutil
```

按终端提示让键盘进入 DFU 模式。UF2 引导程序则使用生成的 `.build/ciank67_default.uf2`。

默认键位中，`RM_*` 控制按键矩阵灯，`UG_*` 控制底灯；自定义 `RGBG_TOG` 同时切换两组灯光。

灯光控制沿用 stable 的松开按键时处理。两组灯状态不一致时，`RGBG_TOG` 会先全部关闭，再按一次全部开启。矩阵灯关闭时保持 I²C 可用，由渲染器发送全黑帧；不在按键处理过程中关闭 I²C。底灯状态指示层不覆盖手动关闭状态；`RGB_M_*` 旧快捷键保留选择灯效和循环灯效组的行为。

WS2812 驱动发送全部 18 颗底灯的数据，沿用 stable 的 6/12 tick 高脉宽和 64 颗黑色尾部数据，并在末尾保持完整的 280 微秒低电平复位间隔。缓冲区使用静态 RAM，灯珠数量在 `keyboard.json` 的 `rgblight.led_count` 中配置。扫描定时器中断只设置待处理标志，键盘、灯光、存储任务在主循环运行。

nRF5 使用独立的启动入口，并在链接时排除工具链的默认 CRT 和 semihosting 系统调用。部分 ARM 工具链的默认启动代码会在进入 `main()` 之前执行 `BKPT 0xAB`，导致脱离调试器运行时灯光、USB 和蓝牙均不启动。默认构建会自动检查最终 ELF 的向量表及 semihosting 指令，也可手动检查：

```sh
python3 util/check_nrf5_firmware.py .build/ciank67_default.elf
```

NKRO 按当前输出驱动的能力选择：USB 使用自己的协议状态，蓝牙只在编译启用 `BLE_NKRO` 且处于 report 模式时使用 NKRO。未启用蓝牙 NKRO 时，即使保存的 NKRO 开关已开启，蓝牙仍发送普通键盘报告。`OU_USB`、`OU_BT` 和 `OU_NONE` 由 nRF5 输出处理接入当前 QMK 的按键处理链。

EEPROM 默认容量为 64 字节（启用 VIA 时为 1024），可通过 `EEPROM_SIZE` 配置。旧版 36 字节记录和之前的 1024 字节记录均按实际长度、容量边界读取。FDS 写入使用独立快照缓冲，空间不足时回收无效记录并重试，队列忙时延后重试；不因这些可恢复错误复位。原生测试还会从 `origin/qmk_tsiank_stable` 读取真实 PWM 驱动，逐样本比较当前输出，验证所有 stable 数据和尾部信号保持一致：

```sh
python3 -m unittest discover -s platforms/nrf5/tests -v
```

重新编译后，务必重新生成 DFU ZIP；旧 ZIP 不会随 BIN 更新。

排查实机灯光问题时，在文本编辑器中使用英文输入法，进入灯光层 `TO(_RGBST)`，按物理 Tab 键，可输出 `ciank67-light-stable-r3` 状态行。输出逐字发送，需要等待数秒。`m` / `u` 分别为矩阵灯 / 底灯的开关和模式，`f` 为矩阵灯标志，`p=1` 表示底灯电源关闭，`twi` 为 I²C 失败次数，`ee` 为最后一次存储错误和累计错误次数，`irq` 应为 0（主循环上下文）。比较关灯前后状态行，可以区分逻辑开关、硬件传输失败和设备复位。

同一灯光层的物理 Q 键可触发 8 秒底灯自检：每步 2 秒，依次为全部 18 颗红色、编号 0～8 绿色、编号 9～17 蓝色、Caps Lock 区域 9～16 白色。自检暂时绕过 rgblight 灯效、指示层和现代像素缓冲区，直接使用 stable 的数组发送接口，不写入 EEPROM。完成后恢复当前灯效和 Caps Lock 指示；原本关闭底灯时恢复关闭。再次按 Q 可提前结束。自检期间不要调节灯光。

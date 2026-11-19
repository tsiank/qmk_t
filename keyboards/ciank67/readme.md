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

灯光控制沿用 stable 的松开按键时处理。两组灯状态不一致时，`RGBG_TOG` 会先全部关闭，再按一次全部开启。矩阵灯关闭时保持 I²C 可用，由渲染器发送全黑帧；不在按键处理过程中关闭 I²C。底灯关闭时，Caps Lock 仍可单独点亮编号 9～16 的白色指示灯，关闭 Caps Lock 后熄灭；`RGB_M_*` 旧快捷键保留选择灯效和循环灯效组的行为。

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

排查实机灯光问题时，在文本编辑器中使用英文输入法，进入灯光层 `TO(_RGBST)`，按物理 Tab 键，可输出 `ciank67-light-music-spectrum-r8` 状态行。输出逐字发送，需要等待数秒。`m` / `u` 分别为矩阵灯 / 底灯的开关和模式，`f` 为矩阵灯标志，`p=1` 表示底灯电源关闭，`twi` 为 I²C 失败次数，`ee` 为最后一次存储错误和累计错误次数，`irq` 应为 0（主循环上下文）。比较关灯前后状态行，可以区分逻辑开关、硬件传输失败和设备复位。

同一灯光层的物理 Q 键可触发 8 秒底灯自检：每步 2 秒，依次为全部 18 颗红色、编号 0～8 绿色、编号 9～17 蓝色、Caps Lock 区域 9～16 白色。自检暂时绕过 rgblight 灯效、指示层和现代像素缓冲区，直接使用 stable 的数组发送接口，不写入 EEPROM。完成后恢复当前灯效和 Caps Lock 指示；原本关闭底灯时恢复关闭。再次按 Q 可提前结束。自检期间不要调节灯光。

音乐律动：默认固件新增 USB 专用 HID 和蓝牙 GATT 频谱通道，Windows 程序及使用方法见 [音乐律动说明](../../util/ciank67_music/README.md)。矩阵灯和底灯仍各自受开关控制，Caps Lock 优先覆盖音乐效果；停止程序或断流后恢复普通灯效。SDK 保持 15.3.0。

USB 身份从 `keyboard.json` 读取：`keyboard_name` 为产品名，`manufacturer` 为厂商名，`usb.vid` / `usb.pid` 为设备 ID。nRF 应用 SDK 配置会将它们映射到 `APP_USBD_STRINGS_PRODUCT` / `APP_USBD_STRINGS_MANUFACTURER` / `APP_USBD_VID` / `APP_USBD_PID`。ciank67 当前为 `ciank67`、`Tsiank`、`FEED:C475`；修改后重新编译并刷入。DFU 引导程序的 USB 身份由其独立配置决定。

R7 修正了应用 USB 沿用 Nordic 默认 VID/PID 和产品名的问题。升级后重新拔插 USB，让 Windows 枚举新增的音乐接口。客户端同时兼容旧应用身份 `1915:520B` 和当前 `FEED:C475`，始终以厂商音乐 Usage `FF67:0067` 筛选，不按产品名匹配，也不会选择普通键盘/鼠标接口。

R8 新增独立 RGB Matrix 灯效 `MUSIC_SPECTRUM`：16 个频段由左至右显示，按键的五排组成自下而上的频谱柱，带白色峰值、250 ms 峰值保持和平滑回落。音乐数据到来时自动临时使用此效果，不修改保存的灯效；停止客户端或断流后恢复原灯效。灯光层的物理 W 键可直接选择该效果，也可用矩阵灯效循环键选择；手动选择后，无数据时回落至全黑。原有矩阵色相、饱和度、亮度按键仍有效，速度按键控制柱和峰值的回落速度。灯光开关和 Caps Lock 指示优先级保持原样。现有客户端无需更新。

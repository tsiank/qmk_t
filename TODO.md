# 后续工作

## ciank67 无线功耗（原理图审查完成，待实测后实施）

- [ ] 先完成 R13 无线 VIA/控制/律动/MIDI 实板验证，再单独修改功耗策略，保留当前回归基线。
- [ ] 关矩阵灯后让 IS31FL3741 进入软件 shutdown，开启前恢复配置；保持共享 I²C 和 OLED 可用，禁止以关闭整个 I²C 代替灯控关断。
- [ ] 核对实际 PCB 的 R56：当前 `.sch` 中为 VCC → RGBLT_VCC 的 0Ω 旁路，装上则 Q2 不能断电；旧 `.net` 与当前图不一致，不能作为当前硬件接线依据。
- [ ] 核对 Q2/R11/R57/R58 的实物装配，以及 Q2 源、栅、漏电压；3.3V GPIO 直接控制电池/VCC 侧 P-MOS 未必可靠关断，评估电平转换或负载开关。
- [ ] 关 WS2812 前发送黑帧并确认 PWM 停止、DIN 为低；测关灯后 RGBLT_VCC 和支路电流，区分静态耗电、MOS 未关断及 DIN 反灌。保持 Caps 指示覆盖功能。
- [ ] 对比 PWM0 STOP 与 disable/uninit 后的电流，需再次点亮时恢复；不影响 PWM1 音频。
- [ ] 测两灯关闭时 SDA/SCL 是否都保持高；2kΩ 上拉的单线长期拉低约 1.65mA，不能靠拉低总线省电。
- [ ] 分别测固定 BLE、自动模式且接收器拔出、2.4G 空闲/输入/律动的电流；减少无接收器的周期探测和 RF 空闲完整快照，忙时保持低延迟。
- [ ] 评估空闲降低矩阵扫描频率、BLE 空闲连接参数、RF 空闲心跳/按需高速轮询与可唤醒休眠；当前 RF 模式刻意禁用自动 System OFF。
- [ ] 记录硬件 DCDCEN、VBUS/USBD 状态及睡眠电流；配置宏已经请求 DCDC，先验证运行状态，不盲目改宏。

详见 [原理图与固件功耗审查](docs/nrf5_esb.md#ciank67-无线功耗审查待实测)。

## nRF5 split keyboard（暂缓，待确认硬件）

- [ ] 确定左右半边的矩阵、nRF52840 主控、互联方式和固定主半；首版不做任意侧 USB 主半切换。
- [ ] 复用 QMK split 矩阵与层/旋钮/灯光同步，新增公共 nRF5 传输后端，避免每个键盘复制实现。
- [ ] 有线方案评估硬件 UART；无线方案优先复用 [ESB / Timeslot 传输层](docs/nrf5_esb.md)，BLE 私有左右链路作为备选。
- [ ] 在 ciank67 → ciank_pad 2.4G 报告转发验证后，增加矩阵 / 旋钮事务；主半统一处理 QMK 层和组合键，区别于两把独立键盘的 HID 合并。
- [ ] 无线时区分电脑连接与左右连接的角色、句柄、配对与重连，排除从半触发电脑 HID/MIDI 状态变更。
- [ ] 如采用 BLE 左右链路，再调整 Central/Peripheral 连接资源、启动顺序、主从判断和 RAM 配置；优先保留现有 SDK 15 / S140。
- [ ] 实现有序按键/旋钮队列、周期完整状态、断链清键与恢复；主循环消费事件，不在 BLE 中断执行 QMK 逻辑。
- [ ] 扩展音乐频谱、校时、屏幕模式与双侧电量同步；VIA 和客户端统一连接主半。
- [ ] 使用两块实际硬件验证 USB/BLE、断连、休眠、重连和连续输入。

参考：[QMK split](https://docs.qmk.fm/features/split_keyboard)、
[Nordic 多角色说明](https://docs.nordicsemi.com/r/bundle/sds_s140/page/sds/s1xx/ble_protocol_stack/role_configuration.html)。

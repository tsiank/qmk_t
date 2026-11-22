# 后续工作

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

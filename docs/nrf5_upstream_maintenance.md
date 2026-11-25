# nRF5 与官方 QMK 的兼容边界

## 固定基线

`platforms/nrf5/upstream.json` 是升级检查的入口，记录官方仓库、已合入的提交、
nRF5 SDK 提交、子模块覆盖和每处共用文件补丁的原因。当前上游基线为
`80973aa0b5413e9a5bbbf9293507f39e870f9aa3`（2026-11-18），不是运行检查时的最新远端。
SDK 固定为 gitlink 指向的 `ff4ae8ffce36c123fe0981b0f1a4089afbb0a3cd`，不随 SDK 的
`master` 分支自动升级。SDK 与 QMK 升级应分别验证。

执行只读检查：

```sh
python3 util/nrf5/check_upstream.py
python3 util/nrf5/check_upstream.py --candidate upstream/master
```

第二条需要先自行获取目标引用。脚本不联网、不创建分支、不合并、不修改工作区。
它检查共用文件补丁是否登记、SDK 是否为干净的固定版本，以及灯光 / OLED / 宏兼容
入口是否混入 Nordic 依赖。目标引用检查还列出官方改动与已登记补丁重叠的文件。
重叠是人工审查清单，不表示这些文件必然存在文本冲突；无重叠也不保证行为兼容。

## 代码归属

| 部分 | 位置与约束 |
| --- | --- |
| GPIO、底灯电源、上电等待、静态指示重发 | `platforms/nrf5/lighting_runtime.c` |
| 省电 / 输出 / 射频 / EEPROM 调度 | `platforms/nrf5/` 和 Nordic 协议后端 |
| SDK 宏兼容与头文件顺序 | `platforms/nrf5/_compiler_support.h` |
| 屏幕唤醒策略 | `platforms/nrf5/features/oled_power.c` |
| 通用效果、层叠加与用户设置 | 官方 `quantum` 状态机，通过少量通用钩子接入 |
| JSON 元数据、物理矩阵、灯珠位置与板级选择 | `keyboards/<board>/` |
| 客户端协议、音乐、MIDI、屏幕、旋钮共用功能 | `platforms/nrf5/features/`，不复制到各板目录 |

底灯核心仅提供 `rgblight_platform_init/prepare/finish/task` 四个弱钩子；默认实现
不改变官方行为。平台实现负责电源和闲置帧处理，层叠加仍由 QMK 完成。
`task` 接收动画状态，不读取 QMK 私有全局变量；有变化时最多重发两次当前合成帧，
不缓存旧 Caps 状态。关底灯仍能点亮指示层，闲置时仍只清黑一次并断电。
底灯电源随实际帧提交，动态效果从第一帧开始获得电源；不会修改 enable / HSV / EEPROM。

矩阵灯通过 `rgb_matrix_platform_suspended()` 查询临时挂起状态，音乐通过已有的
`rgb_matrix_override_effect()` 临时选择效果。关闭或挂起时不会调用效果覆盖，保存的
模式不变。OLED 使用 `oled_platform_can_wake()`；默认允许，Nordic 实现负责常亮、
强制熄屏和无线空闲策略。共用核心不包含 Nordic GPIO、SDK 或无线管理头文件。

`rgb_pwr_on/off` 暂保留为平台侧的旧板级兼容接口，声明移到
`platforms/nrf5/lighting_runtime.h`。新业务代码优先走正常帧渲染接口。

Community Modules 适合独立按键行为和通用业务扩展，但当前无线调度、硬件供电和
SDK 兼容属于平台后端。此次不将它们伪装成模块，也不改变已经共用的板级入口。
以后新增纯按键行为时应先评估官方模块钩子；不足时只增加必要的通用接口，不能在
`quantum` 中堆叠更多平台业务。参考 [Community Modules](https://docs.qmk.fm/features/community_modules)。

## 仍需保留的补丁

完整路径和原因见 manifest。主要分组及验证要求：

| 分组 | 保留原因 | 升级验证 |
| --- | --- | --- |
| 构建、JSON schema、处理器识别 | QMK 尚需识别本分支的 NRF52840 后端 | 三种 nRF 配置及普通 AVR RGB 配置编译 |
| 时钟 / EEPROM / SDK 宏 | RTC 回绕、异步 Flash、宏定义归属与官方默认不同 | 回绕、Flash 保存失败、C/C++ 编译 |
| 输出与 MIDI | USB / BLE / ESB 路由和独立演奏状态 | 模式切换、MIDI 队列、音频共存 |
| VIA 旋钮与层缓存 | 当前动态编码器事件后端需保持按下/释放的源层 | 换层旋转、编码器事件释放与保存 |
| 灯光 / OLED 通用钩子 | 临时渲染与硬件生命周期不等同用户开关 | 原有灯光、Caps、休眠、共享 I²C 测试 |
| IS31FL3741 | 传输重试、全黑休眠、恢复完整 PWM 上传 | 总线错误、关灯、重新点亮和持续律动 |

不要为了减少文件数复制一整份官方灯光或层状态机。能用于其他平台的修正可以单独
整理为上游补丁；提交给官方之前仍需确认其接口和行为约定。已经失去作用的登记项
会出现在 `obsolete_inventory_entries` 中，验证后删除补丁与登记，而不是长期保留。

数字字符串 GPIO 与 AVR 的 `F6` 等端口名必须匹配不同的 schema `oneOf` 分支。
旧分支用一个同时匹配端口名与数字的正则，导致普通 AVR JSON 配置验证失败；现已
分离，并加入 `F6` / `31` 等实际校验测试。

## 集成与验证流程

1. 先提交当前开发工作，保留已验证的固件和构建信息。
2. 获取官方目标提交，在单独的集成分支或 worktree 合并；先运行 candidate 重叠检查。
3. 根据官方 [Breaking Changes](https://docs.qmk.fm/breaking_changes) 审查行为变化，
   逐项处理 manifest 中的补丁，不只解决文本冲突。
   在集成分支中将 manifest 基线改为目标官方提交，并更新补丁登记，再执行检查。
   否则旧基线到新官方之间的正常改动也会被误认为本地补丁。这时的新基线仍是待验证状态。
4. 运行 `python3 -m unittest discover -s platforms/nrf5/tests` 和下面四项编译：

   ```sh
   make ciank67:default
   make ciank_pad:test
   make ciank_pad:esb_rx
   make handwired/onekey/promicro:rgb
   ```

5. 真机检查 USB / BLE / ESB、首次配对与重连、至少 30～60 分钟闲置后的首键、Caps
   指示、灯光开关与律动停止恢复、MIDI、VIA 保存，以及 OLED / 旋钮共享 I²C。
6. 验证完成后一起提交新的官方基线和补丁登记，并记录已验证版本；子模块 pin
   只在独立验证其升级时修改。

`.github/workflows/nrf5.yml` 在本分支 push、相关 PR 和手动触发时执行检查。它运行
原生测试和四项编译，按目标保存日志与构建文件；没有刷写步骤。CI 中各目标在独立
工作区运行，本地四项 make 应依次运行。新增 workflow 的远端执行结果以 GitHub
Actions 为准，本地测试通过不能写成已通过远端 CI。

本轮不引入 RTOS，不改变无线包格式、EEPROM 布局、客户端协议或键位。

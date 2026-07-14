# Cardputer ADV → Mac 无线语音键盘 · 第一期实施计划

## 背景与硬约束

- 设备:M5Stack **Cardputer ADV**(ESP32-S3FN8,512KB SRAM,8MB 外挂 PSRAM 未启用,8MB flash,ES8311 codec + 高信噪比 MEMS 麦克风,WiFi 2.4GHz,BLE only 无经典蓝牙)。已打通 PlatformIO 编译→烧录→运行闭环(espressif32@7.0.1,Arduino core 3.x,M5Unified 0.2.18 已确认支持 ADV,含 ES8311 mic/speaker 回调)。
- Mac:用户主力机,macOS,Python 3.10 可用,无 Homebrew。串口 `/dev/cu.usbmodem1401`。
- **核心诉求**:用户坚持用 Mac 上的 **Typeless**(第三方 STT 应用,监听系统麦克风)做语音识别 → Cardputer 的音频必须以"麦克风输入"形态到达 macOS → 方案:WiFi 音频流 → 桥接程序 → **BlackHole 虚拟声卡** → Typeless 选 BlackHole 为输入。
- 键盘走桥接注入(用户已拍板,不走 BLE HID):按键事件 WiFi 发给桥接,桥接用 CGEvent 注入 macOS。
- 用户是嵌入式新手,交付节奏要"每个里程碑可见可玩"。

## 总体架构

```
Cardputer 固件(单固件多模式,主菜单)
  ├─ 模式:语音(push-to-talk 采音 16kHz/16bit/mono → UDP 流)
  ├─ 模式:键盘(按键事件 → TCP JSON)
  └─ 设置:WiFi 配置(设备上输 SSID/密码,存 NVS)、配对、状态栏

Mac 桥接(Python 3.10 守护进程,v1 命令行,菜单栏 UI 留二期)
  ├─ mDNS 广播 _cardbridge._tcp(zeroconf)
  ├─ 配对:首连显示 6 位码,Cardputer 上输入 → 发 token(NVS 持久化),后续凭 token
  ├─ 控制/键盘通道:TCP 长连接(JSON 行协议,token 鉴权,心跳)
  ├─ 音频通道:UDP(带序号的 PCM 帧,20ms/帧≈640B,jitter buffer ~100ms)
  │    → sounddevice/CoreAudio 输出到 BlackHole 2ch(必要时重采样到设备采样率)
  └─ 键盘注入:Quartz CGEventPost(需用户授一次「辅助功能」权限)
```

## 里程碑(每步可验证)

- **M1 地基**:PSRAM 启用(memory_type 需实测:先试 `qio_opi`,启动失败回退 `qio_qspi`;以 `ESP.getPsramSize()≈8MB` 为准)。固件主菜单骨架 + WiFi 配置页(键盘输入 SSID/密码存 NVS)+ 连上路由器 + mDNS 发现桥接 + 配对握手。验收:屏幕显示"已连接 <Mac 名>"。
- **M2 键盘**:键盘模式端到端。Cardputer 敲字 → Mac 前台应用出字。含修饰键(Shift/Ctrl/Opt/Cmd 映射 Cardputer 的 fn 层)、退格、回车、方向键。验收:在 Mac 的文本框里正常打一段话。
- **M3 语音**:按住指定键说话(push-to-talk)→ 松开停止。音频经 UDP → 桥接 → BlackHole。验收分两步:①QuickTime/录音机选 BlackHole 能录到清晰人声;②Typeless 选 BlackHole,说话出正确文字。屏幕显示电平表 + 传输状态。
- **M4 打磨**:断线自动重连(WiFi 断/桥接重启/Mac 睡醒)、配对信息持久化、菜单状态栏(WiFi/桥接/电量)、语音键盘两模式共存切换。

## 关键技术决策与理由

1. **音频走 UDP、键盘走 TCP**:音频容忍丢包不容忍延迟(丢一帧 20ms 无感);按键一个都不能丢且要保序。
2. **16kHz/16bit/mono**:STT 模型(含 Typeless 底层)内部基本都是 16k;32KB/s 对 2.4G WiFi 毫无压力。ES8311 原生支持。若实测 Typeless 对采样率挑剔,桥接端软件重采样到 BlackHole 设备采样率(48k)兜底。
3. **push-to-talk 而非常开麦**:省电、避免误触发、符合"对讲机"心智(参考 cardputer-claude-os 的按住空格说话)。键位待定,倾向长按 `Fn` 或空格,菜单可改。
4. **配对安全**:首连 6 位码人工确认 + 之后 token(HMAC)鉴权;桥接只监听局域网;音频/按键只接受已配对设备。防"邻居的 Cardputer 打你的字"。
5. **多 Mac 支持(用户需求)**:Cardputer NVS 存「已配对电脑列表」(名称 + token 数组),每台 Mac 独立配对。连接逻辑:mDNS 扫描 → 在线的已配对 Mac 仅一台则自动连,多台则屏幕列表选择;菜单提供「切换电脑 / 添加电脑 / 删除配对」。**硬约束:同一时刻只连一台**(当前活跃目标),按键/音频绝不广播,切换必须显式操作。桥接端无感知(各自只认自己签发的 token)。
5. **桥接用 Python 3.10 起步**:依赖 zeroconf、sounddevice、pyobjc(Quartz)。v1 先 CLI(终端跑),稳定后二期包成菜单栏 App(rumps/SwiftUI)。理由:迭代最快,用户已有 Python 环境。
6. **BlackHole 安装**:官方 pkg 下载安装(需用户点一次密码),不依赖 Homebrew。

## 风险与预案

- **R1 PSRAM 模式选错开机卡死**:两种 memory_type 逐个试,卡死无损失(重烧即可)。M1 首项解决。
- **R2 M5Unified 的 ADV mic API 采样质量/接口坑**:M3 前先写一个最小录音验证程序(录 2 秒存 PSRAM 打印电平统计),确认 `M5.Mic` 在 ADV 上可用;不行则降级直接用 ESP-IDF I2S + ES8311 寄存器驱动(M5Unified 源码里有现成寄存器序列可抄)。
- **R3 CGEvent 注入被 macOS 权限拦截**:首次运行引导用户在「系统设置→隐私与安全→辅助功能」勾选终端/桥接;文档化。
- **R4 WiFi 抖动导致音频卡顿**:jitter buffer 100ms 起步可调;丢包直接丢帧不重传;实测家庭网络。
- **R5 Typeless 不认 BlackHole 或效果差**:先用系统录音机验证音质排除我方问题;Typeless 侧不可控则备选 aggregate device 或提高采样率。
- **R6 端口/串口抖动影响调试**:已知 HWCDC 端口偶发消失,烧录时轮询等端口,必要时手动 G0 进下载模式(已验证流程)。

## 明确不做(第一期)

- BLE HID 键盘(用户拍板不需要;二期可加)
- Session 监控/AI 状态推送(第二期,桥接架构已为其预留 TCP 控制通道)
- 从 Cardputer 反向注入指令到 Claude 会话(第三期,需单独调研可行性)
- 菜单栏 GUI、内网穿透/外网访问
- (注:多 Mac 配对已纳入第一期,见决策 5;M1 先做单 Mac 跑通,M4 补全列表管理 UI)

## 交付物

- 固件:`/Users/chaos/m5 cardputer`(PlatformIO 工程,现有 Hello World 演进)
- 桥接:`/Users/chaos/m5 cardputer/bridge/`(Python,含 README:BlackHole 安装、权限授予、Typeless 设置步骤)

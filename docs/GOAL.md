# GOAL — Cardputer ADV 无线语音键盘 + Agent 监控终端

> 本文档是本仓库的**唯一目标定义**(single source of truth)。接手开发前请通读。
> 分工:Codex 负责实现;Claude 负责验收、烧录与实机联调。**开发阶段不要烧录**(见「开发约束」)。

## 1. 产品目标

把 M5Stack **Cardputer ADV** 变成 Mac 的无线 vibe-coding 伴侣,取代用户现有的 DJI Mic + 小手柄:

1. **无线麦克风**:设备麦克风音频通过 WiFi 持续推流到 Mac,注入 BlackHole 虚拟声卡,使 Mac 上的 **Typeless**(第三方听写 App,监听系统麦克风)能把它当普通麦克风使用。**不做设备端语音识别**——用户明确只信 Typeless。
2. **无线键盘**:Cardputer 56 键键盘按键经 WiFi 发到 Mac,由桥接程序注入系统(CGEvent)。其中一个专用键映射为 Typeless 的"按住录音"热键,实现"按住说话、松开出字"。
3. **(二期)Agent 助手**:在设备屏幕上查看/管理 Mac 上 Claude Code 与 Codex CLI 的 session 状态,任务完成时设备提醒。一期只需**预留协议与 UI 框架**。

## 2. 系统形态(用户确认的 OS 结构)

```
常驻服务(无 UI,连接建立即生效,与当前页面无关):
  🎤 麦克风推流(UDP)    ⌨️ 键盘透传(TCP)
  顶部状态栏:WiFi名/信号 · 已连接Mac名 · 🎤开/静音 · 电量

屏幕页面(主菜单三项):
  ① Claude 助手   —— 二期实现,一期放占位页("Coming soon")
  ② Codex 助手    —— 二期实现,一期放占位页
  ③ 设置          —— WiFi 管理 / 电脑(配对)管理 / 麦克风静音 / 亮度 / 熄屏时间
```

### 2.1 WiFi 管理(用户强调的体验要求)
- **扫描列表选网**:进入页面自动扫描,列出 SSID/信号/加密标记,上下键选择,**只需输入密码**(禁止让用户手输 SSID)。
- **已保存网络管理**:列表显示已存网络与当前连接;支持切换连接、忘记网络;密码存 NVS;开机自动连接已知网络(可按信号优先)。
- 全部已知网络均连不上时,自动进入扫描页。
- 仅 2.4GHz;不支持 802.1X 企业认证(无需实现)。

### 2.2 电脑(配对)管理
- 已配对 Mac 列表:名称 + 在线/离线 + 当前连接标记。
- 操作:连接这台 / 断开 / 删除配对 / 添加新电脑(触发 6 位码配对流程)。
- 多台在线时手动选择,记住上次选择;**同一时刻只连一台**,按键/音频绝不广播。

### 2.3 语音链路(关键设计,已与用户对齐)
- 设备连上 Mac 后**持续推流**(在 Mac 眼里 = 一直插着的 USB 麦克风)。
- **录音起止完全由 Typeless 自己的热键机制控制**:设备只是把专用键(建议默认映射 F13,可配置)作为普通键盘事件发过去。设备端**没有** push-to-talk 逻辑。
- 屏幕常驻麦克风指示;设置里可**一键静音**(静音 = 停止 UDP 推流)。
- 桥接断开 / Mac 不在线时自动停流省电。

## 3. 架构与协议

```
┌─ Cardputer 固件(C++ / PlatformIO / Arduino core 3.x / M5Unified)─┐
│ FreeRTOS 任务:mic采集→UDP发送 │ TCP控制+键盘 │ UI/键盘扫描 │ WiFi管理 │
└──────────────────────────────────────────────────────────────────┘
        │ mDNS 发现 _cardbridge._tcp        │
        │ TCP 7788(控制+按键,JSON行协议) │ UDP 7789(音频帧)
┌─ Mac 桥接(Python 3.10,bridge/ 目录)────────────────────────────┐
│ zeroconf广播 │ 配对/token鉴权 │ UDP收流→sounddevice→BlackHole 2ch │
│ CGEvent按键注入(pyobjc/Quartz) │ (二期)agent状态收集与推送     │
└──────────────────────────────────────────────────────────────────┘
```

### 3.1 配对与鉴权
1. 设备 mDNS 发现桥接 → TCP 连接 → 发 `{"t":"hello","dev_id":"<mac地址>","token":null}`。
2. 无有效 token 时桥接生成 6 位码显示在 Mac(终端/通知),设备端用户输入 → `{"t":"pair","code":"483291"}` → 校验通过桥接返回长期 token(≥32 字节随机),设备存 NVS(支持多台 Mac 的 token 列表)。
3. 之后凭 token 直接握手。桥接持久化已配对设备表(`~/.cardbridge/config.json`)。
4. 所有 TCP 消息与 UDP 音频帧都必须带 token(UDP 帧头含 token 的短 HMAC 即可,防局域网内他人注入)。

### 3.2 音频(UDP 7789)
- PCM 16kHz / 16bit / mono,**20ms/帧 = 640 字节载荷**,帧头:seq(u32) + 时间戳(u32, ms) + HMAC8。
- 桥接侧 jitter buffer 起步 100ms(可调),丢帧静音填充,不重传。
- 桥接把流写入名为 "BlackHole 2ch" 的输出设备;设备采样率不匹配时软件重采样。README 须写明 BlackHole 安装与 Typeless 选麦步骤。

### 3.3 按键(TCP 7788,JSON 行)
- `{"t":"key","k":"<keycode>","m":["cmd","shift"],"a":"down|up"}` — 必须区分 down/up(Typeless 热键靠按住)。
- 键位映射:Cardputer 的 fn 层 → macOS 修饰键/方向键/F13;映射表放独立源文件,便于调整。
- 心跳 `{"t":"ping"}`/`{"t":"pong"}` 5s 间隔,3 次无响应视为断线,进入自动重连。

### 3.4 为二期预留
- TCP 协议加消息类型 `{"t":"agent_status",...}`(桥接→设备)与 `{"t":"agent_list_req"}`(设备→桥接)即可,一期不实现内容,但解析器遇到未知 `t` 必须忽略而非断连。

## 4. 里程碑与验收标准

| # | 内容 | 验收(由 Claude 实机执行) |
|---|------|--------------------------|
| M1 | WiFi 扫描/密码输入/NVS 多网络管理 + mDNS 发现 + 6位码配对 + 状态栏 | 设备扫到家里 WiFi 并连接;与 Mac 桥接完成配对;屏幕显示"已连接 <Mac名>";重启后全自动重连 |
| M2 | 键盘透传 + F13 专用键 | 在 Mac 文本框用 Cardputer 打出一段含大小写/数字/标点的英文;修饰键组合(Cmd+C 等)生效;按住专用键 Typeless 开始录音、松开停止 |
| M3 | 麦克风持续推流 → BlackHole | QuickTime 选 BlackHole 能录到清晰人声;Typeless 选 BlackHole 后按住热键说话出正确文字;屏幕电平表工作 |
| M4 | 打磨 | 断 WiFi/关桥接/Mac 睡眠后 30s 内自愈重连;静音键生效;多 Mac 列表管理可用;熄屏省电可配置 |

## 5. 硬件事实(已实测,勿再踩坑)

- 板:Cardputer ADV(Stamp-S3A,ESP32-S3FN8)。**无 PSRAM**——`qio_qspi`/`qio_opi` 均实测 PSRAM ID 读 0x00000000,官方规格表也无 PSRAM;网上"8MB PSRAM"文章是错的。**RAM 预算 = 512KB SRAM(实测可用堆约 356KB),禁止使用 ps_malloc/PSRAM 相关 API**。音频等缓冲必须小而流式(20ms 帧 + 几帧环形缓冲即可)。
- Flash 8MB;当前 app 分区 3.3MB,固件 487KB,空间充足。
- 音频:ES8311 codec + 高信噪比 MEMS 麦克风。**M5Unified 0.2.18 已含 ADV 支持**(`board_M5CardputerADV` 自动识别、`_microphone_enabled_cb_cardputer_adv` 等)。用 `M5.Mic`(M5Unified Mic_Class)采集;若实测有坑,库源码里有 ES8311 寄存器序列可参考直驱。
- 无经典蓝牙(仅 BLE),本项目不用蓝牙。
- WiFi 仅 2.4GHz。

## 6. 开发环境与约束

- 项目路径:`/Users/chaos/m5 cardputer`(**路径含空格,所有命令加引号**)。
- 工具链:PlatformIO 6.1.19 与 esptool 位于 `~/Library/Python/3.10/bin/`(**不在 PATH**,需 `export PATH="$HOME/Library/Python/3.10/bin:$PATH"`)。平台 espressif32@7.0.1,board `m5stack-stamps3`,框架 arduino(core 3.x)。
- 现有 `platformio.ini` 的 `ARDUINO_USB_CDC_ON_BOOT=1` 必须保留(串口日志靠它);**不要添加任何 PSRAM 相关配置**。
- 依赖:`m5stack/M5Cardputer`(引入 M5Unified/M5GFX)。新增库需在 platformio.ini 的 lib_deps 声明并说明理由。
- **开发约束(重要)**:
  - Codex 只做:写代码 + `pio run` 编译通过 + 桥接端可脱机自测。**不要执行 `-t upload` 烧录,不要占用 `/dev/cu.usbmodem1401` 串口**——实机验收由 Claude 与用户执行(串口有 HWCDC 抖动等已知怪癖,已有处理经验)。
  - 桥接(`bridge/`)用 Python 3.10 标准 venv 或 `pip install --user`;Mac 无 Homebrew,勿依赖 brew。依赖建议:zeroconf、sounddevice、pyobjc-framework-Quartz。
  - **写一个 `bridge/fake_device.py` 模拟器**:模拟设备完成配对、发按键、推正弦波音频——让桥接端不插真机就能端到端自测(这是 Codex 自验的主要手段)。
  - 固件代码分模块(wifi_mgr / pairing / audio_tx / key_tx / ui 各自独立文件),`src/main.cpp` 只做装配。
  - 中文注释可用;git 提交信息用英文。

## 7. 已知风险与预案(实现时注意)

1. **M5.Mic 在 ADV 上的采集质量/回调节奏未知** → 音频任务与 UDP 发送解耦(环形缓冲),先保证不丢帧不阻塞;实测由验收阶段反馈。
2. **CGEvent 注入需要「辅助功能」权限** → 桥接首次运行检测权限并打印引导(系统设置→隐私与安全→辅助功能)。
3. **中文输入法下按键注入体验未知** → 一期只保证英文/代码/快捷键场景;中文列为实测项,不阻塞验收。
4. **HWCDC 串口抖动**(端口偶发消失几秒)→ 与 Codex 无关(不碰串口),验收侧已有轮询经验。
5. **多 Mac 切换竞态**(两台桥接同时在线)→ 设备端一次只维护一条 TCP 连接,连接建立前先断旧连接。

## 8. 目录规划

```
/Users/chaos/m5 cardputer/
├── docs/GOAL.md            ← 本文档
├── platformio.ini
├── src/                    ← 固件(main.cpp 装配 + 各模块)
├── bridge/                 ← Mac 桥接(Python)+ fake_device.py + README.md
└── .gitignore              ← 已含 .pio/
```

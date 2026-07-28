# 系统结构

PanPal 运行在 Windows 用户会话中。托盘界面和 Bridge 共用同一份配置、设备身份
和配对数据。

```text
Cardputer ADV
  ├─ 按键与语音控制
  ├─ 16 kHz 单声道麦克风
  ├─ Dapan 桌宠与任务状态
  └─ Wi-Fi 或 Bluetooth LE
             │
             ▼
Windows PanPal
  ├─ TCP/UDP 或 BLE GATT 接收
  ├─ jitter buffer 与音量增益
  ├─ CABLE Input → CABLE Output
  ├─ SendInput 按键与语音快捷键
  ├─ Codex 会话、rollout 和 Hooks
  └─ Tk 设置窗口与系统托盘
```

## Wi-Fi 链路

Bridge 使用 `_cardbridge._tcp` 发布 mDNS 服务。Cardputer 连接 TCP 7788，完成
六位码配对或 token 验证。按键、语音控制和任务状态走这条 TCP 会话。麦克风以
20 ms 为一帧，通过 UDP 7789 发送 PCM16 数据。

UDP 音频带有 HMAC。Bridge 只接收来自已认证 TCP 会话地址的数据包。

## 蓝牙链路

Cardputer 在 Bluetooth 模式下作为 BLE Peripheral。Windows Bridge 扫描指定
服务 UUID，连接后订阅控制与音频特征。控制消息沿用 Wi-Fi 的 JSON 协议；音频
先编码为 IMA ADPCM，再按 MTU 分片发送。

Cardputer 只运行用户选中的传输。Bridge 可以监听两种传输，但同一个 device ID
只保留一条有效会话。

## 语音路径

```text
Cardputer 麦克风
→ PCM16 / IMA ADPCM
→ PanPal jitter buffer
→ sounddevice 输出到 CABLE Input
→ Windows 的 CABLE Output 麦克风
→ 当前语音输入软件
```

按住说话时，`VoiceInputController` 保存当前默认麦克风，将默认设备改为
`CABLE Output`，随后触发配置的快捷键。结束时先恢复麦克风，再结束快捷键。
自动 Enter 开启后延迟 1400 ms 发送。

## Codex 状态

`CodexMonitor` 每三秒调用 `thread/list` 获取最近任务。`RolloutStateReader` 只读取
rollout 文件中的生命周期字段。Hooks 通过本机 UDP 7790 上报工具、授权和结束
事件。`AgentStore` 统一生成发往 Cardputer 的简短快照。

官方额度由 `CodexMonitor` 每 30 秒调用 App Server。选择自定义额度源时，
`CustomQuotaMonitor` 每 2 分钟从只读 HTTPS 接口取得各账户的每周窗口并计算平均
剩余百分比；兼容固件的 5H 与 WEEKLY 字段接收同一个平均值。任务列表仍由 App
Server 提供。自定义请求失败时保留最后一次成功快照。

状态通道不保存会话正文、推理、命令或工具输出。

## 配置与身份

普通配置保存在 `%APPDATA%\CodexDeck\config.json`。配对 token 和自定义额度密钥
使用 DPAPI 加密，分别写入独立目录中的 `.dpapi` 文件。应用 token、BLE bond 和
Cardputer NVS 记录共同组成重连状态。

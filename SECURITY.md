# PanPal 安全说明

PanPal 在 Windows 用户会话中运行，通过局域网或 Bluetooth LE 连接已配对的
Cardputer。Wi-Fi 模式监听 TCP 7788 和 UDP 7789；Bluetooth 模式使用 Windows
BLE GATT。

配对 token 使用 DPAPI 加密，密文保存在当前用户的
`%APPDATA%\CodexDeck\pairing-secrets`。Bridge 会验证控制消息和音频帧，未完成
认证的设备不能发送按键或麦克风数据。

自定义额度只读密钥也使用 DPAPI 加密，保存在
`%APPDATA%\CodexDeck\quota-secrets`。额度请求只接受 HTTPS 或本机回环地址，
不跟随重定向，避免 Authorization 请求头被带到其他服务器。

## 报告漏洞

未修复的问题请通过 GitHub Security Advisory 私下报告，并附上受影响版本、
Windows 版本、复现步骤和影响范围。报告中不要放入真实 token、Wi-Fi 密码、
API Key、`auth.json`、Codex 会话正文或录音。

## 约束

- 日志不得记录配对 token 和 Wi-Fi 密码；
- 日志和普通配置不得记录自定义额度密钥；
- Cardputer 状态消息不得包含提示词、会话正文、推理、命令或工具输出；
- 认证完成前拒绝设备控制消息；
- SendInput 只在当前 Windows 用户会话中运行；
- 发布文件附带 SHA-256，代码签名状态写进发布说明。

# Windows 构建与部署说明

这个分支提供 Windows Bridge 程序，以及与之配套的 Cardputer ADV 固件。Bridge 接收经过身份验证的键盘和麦克风数据，通过 Windows `SendInput` 注入按键，并将设备传来的音频写入已经安装好的 VB-CABLE 音频端点。

## 完整构建

在项目根目录打开 PowerShell，运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

构建脚本会使用隔离的 `windows/.venv-build` 环境，依次检查生成的版本文件、运行 Python 测试、打包图形界面、编译固件、调用 Inno Setup 生成安装程序，并写出 SHA-256 校验值。

如果电脑还没有安装 Inno Setup 6，请先运行：

```powershell
winget install JRSoftware.InnoSetup
```

当前版本生成的文件如下：

```text
dist/installer/PanPal-1.4.2-setup.exe
dist/firmware/panpal-dapan-0.6.2-build23.bin
dist/firmware/codex-deck-spear-rib.bin
dist/SHA256SUMS.txt
```

当前安装包没有进行 Windows 代码签名。如果要分发给更多用户，建议使用自己的代码签名证书进行签名；否则 Windows SmartScreen 可能会显示“未知发布者”警告。

## 安装和配对

1. 单独安装 [VB-CABLE](https://vb-audio.com/Cable/)。本项目不会捆绑或重新分发它。
2. 安装 `PanPal-1.4.2-setup.exe`。安装程序会为 Wi-Fi 模式添加 TCP 7788 和 UDP 7789 防火墙规则；Bluetooth 模式不需要这些规则。
3. 打开 PanPal 设置：
   - 音频写入设备选择 **CABLE Input**。
   - 临时默认麦克风选择 **CABLE Output**。
4. 刷入任意一个固件 `.bin` 文件。两个文件名对应的固件内容完全相同。
5. Cardputer 第一次启动时选择 **Wi-Fi** 或 **Bluetooth**。设备只会初始化选中的无线传输方式。以后切换连接方式时，设备会保存设置并重新启动，不会自动回退到另一种连接。
6. 在 Cardputer 中打开 **Computers > Add computer**。Bluetooth 模式会开放一个持续 60 秒的配对窗口。在 Cardputer 上输入 Windows 显示的六位配对码。

关闭设置窗口后，PanPal 会继续在系统托盘运行：

- 绿色：Cardputer 已连接。
- 黄色：正在等待连接，或有配置问题需要处理。
- 红色：Bridge 运行失败。

安装程序会为当前用户添加登录启动快捷方式。托盘菜单支持打开设置窗口、重启 Bridge 和退出程序。

## 按住说话

- 按住 G0（BtnA）或 `Fn+Space` 开始说话，松开后结束。
- 双击 G0 可以锁定长时间录音；再次单击 G0 结束录音。
- 固件发送的是明确的 `voice down/up/lock` 语音控制事件。`Fn+Space` 不会直接输出 F13；Windows Bridge 会执行用户在设置界面中配置的快捷键。
- 开始说话时，Windows 会先记住当前默认麦克风，然后将默认麦克风切换为虚拟麦克风，最后触发语音输入软件的快捷键。
- 结束说话时，Windows 会先恢复之前的麦克风或指定麦克风，再松开快捷键，或者再次触发切换式快捷键。
- 语音输入软件可以配置成“按住快捷键”或“按一下切换”两种模式。
- 在 Cardputer 上按 `Fn+Enter` 开关“语音结束后自动发送 Enter”，绿色回车图标表示已开启。Bridge 会在语音结束后等待 1 秒再发送，给语音软件留出提交文字的时间；若这 1 秒内开始下一轮语音，旧的 Enter 会自动取消。
- 开启键盘转发后，Cardputer 上普通的 Enter 键也会以明确的按下/松开事件转发给电脑。
- 独立的麦克风图标只会在 Windows Bridge 确认麦克风切换和快捷键注入成功后变绿。
- `Fn+Tab` 只负责在本地操作与电脑键盘转发之间切换，不负责建立连接。键盘图标为青色时表示 Remote 模式且 Bridge 已连接；红色斜线表示已选择 Remote 但连接不可用。

## Codex 状态集成

PanPal 会查找以下位置提供的官方 `codex.exe`：

- VS Code Codex 扩展内置的 `codex.exe`。
- ChatGPT/Codex Windows 客户端内置的 `codex.exe`。
- PATH 或 Codex CLI 安装位置，作为备用方案。

程序通过官方 app-server 的 `thread/list` 接口读取共享的 Codex 状态数据库，不会根据进程名称或窗口标题猜测 Codex 的运行状态。

可选的全局 Codex Hooks 可以为 ChatGPT/Codex 客户端、VS Code 和 CLI 提供更实时的生命周期事件。可以在 Windows 设置界面中安装或移除 Hooks。修改后需要重启 Codex，并由用户亲自确认 Codex 显示的信任提示。

Bridge 只会向 Cardputer 发送经过隐私裁剪的简短状态，不会转发提示词、会话全文、推理过程、原始命令、工具参数或工具输出。

## 企业级 Wi-Fi

Cardputer 可以识别 WPA2-Enterprise 网络，并支持使用用户名和密码登录的 EAP-PEAP/MSCHAPv2。

目前不支持：

- EAP-TLS。
- 客户端证书配置。
- 匿名身份。
- 仅支持 TTLS 的网络。
- 需要网页认证的 Captive Portal 网络。

为了兼容较旧的校园网 RADIUS 服务，这个固件可以协商 supplicant TLS 1.0 至 TLS 1.2。该实现与已经验证可用的旧固件保持一致，但没有配置 RADIUS CA 证书，因此无法从密码学层面验证服务器是否为真正的校园网服务器，存在连接到仿冒热点的风险。

不要公开真实的校园网用户名、密码、配对 token、包含 Codex 内容的日志或录音文件。

正确的固件启动信息应包含：

```text
Firmware: 0.6.2 build 23, protocol 2.4
Enterprise TLS: supplicant compatibility 1.0-1.2
```

## 诊断信息

- 日志：`%LOCALAPPDATA%\CodexDeck\logs\bridge.log`
- 不包含秘密信息的设置：`%APPDATA%\CodexDeck\config.json`
- 配对 token：使用当前 Windows 用户的 DPAPI 加密，保存在 `%APPDATA%\CodexDeck\pairing-secrets`

如果需要在不注入按键、不输出音频的情况下进行诊断，可以运行：

```powershell
.\CardBridge.exe --dry-run --no-audio -v
```

BLE 真机通信、24 小时稳定性，以及 Windows 默认麦克风的真实切换流程，需要在安装了蓝牙适配器和 VB-CABLE 的目标电脑上继续验收。

Bluetooth 首次配对成功后，Cardputer 和 Bridge 会分别保存应用 token，Windows 会保存 BLE bond。以后无论电脑重启、Bridge 重启还是固件重启，都由 Bridge 扫描 Cardputer 广播并自动恢复 GATT Session，不应再次输入六位码。若 Windows 留有旧固件的不完整 GATT 缓存，Bridge 会自动删除并重建系统 bond，但保留应用 token，因此也不需要手动进入 Windows 蓝牙设置删除设备。

只有以下情况才需要重新打开 Cardputer 的 `Add computer` 并输入六位码：主动删除了 Cardputer 中的电脑记录、清除了固件 NVS、卸载时删除了 `%APPDATA%\CodexDeck` 配对数据，或更换了 Windows 用户。

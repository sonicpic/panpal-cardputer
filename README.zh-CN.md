# PanPal

[English](README.md) | [简体中文](README.zh-CN.md)

PanPal 把 M5Stack Cardputer ADV 接到 Windows 电脑上。Cardputer
可以当无线麦克风和小键盘使用，屏幕会显示 Codex 任务状态与 Dapan 桌宠动画。

当前版本为 PanPal 1.4.6，配套固件为 0.6.5。PanPal 是社区项目，与 OpenAI
无隶属关系。

项目源自
[`voltwake/cardputer-codex-deck`](https://github.com/voltwake/cardputer-codex-deck)，
仓库保留了原提交历史、MIT 许可证和版权声明。部分程序文件、配置目录、协议名和
音频设备仍使用 `CardBridge`，已有安装可以沿用原来的配对与系统权限。

## 平台支持

| 电脑系统 | 连接方式 | 麦克风输出 | 电脑端程序 |
| --- | --- | --- | --- |
| Windows 10/11 | Wi-Fi 或 Bluetooth LE | VB-CABLE | PanPal 托盘程序和 Inno Setup 安装包 |

Cardputer 每次只运行一种连接方式。切换 Wi-Fi 或蓝牙后，设备会保存选择并重启。

## 已实现的功能

- 按住 G0/BtnA 或 `Fn+Space` 说话。双击 G0 可以锁定长时间语音，再按一次结束。
- Cardputer 麦克风通过 Windows 的音频输入设备进入语音软件，并触发
  电脑端配置好的快捷键。
- 普通按键、方向键和 Enter 可以转发到电脑。
- Cardputer 最多显示八个近期 Codex 任务，来源包括 ChatGPT 桌面端、VS Code
  Codex 扩展和 Codex CLI。设备只接收标题和简短状态，不接收会话正文与工具输出。
- Dapan 动画、状态色和提示音会对应运行、等待输入、完成、阻塞与断线状态。
- Wi-Fi 支持普通 WPA/WPA2 网络，也支持 PEAP/MSCHAPv2 企业网络。
- 局域网禁止设备互访时，可以改用 Bluetooth LE。蓝牙链路使用 GATT 传控制数据，
  麦克风音频采用 IMA ADPCM。

## Windows 快速安装

1. 安装 [VB-CABLE](https://vb-audio.com/Cable/)。
2. 从 Release 资源中安装 `PanPal-1.4.6-setup.exe`。
3. 打开 PanPal 设置，将音频写入设备设为 `CABLE Input`，临时麦克风设为
   `CABLE Output`。
4. 给 Cardputer ADV 刷入 `panpal-dapan-0.6.5-build26.bin`。
5. 在设备上选择 Wi-Fi 或 Bluetooth，进入 **Computers > Add computer**，输入
   电脑显示的六位配对码。

关闭设置窗口后，PanPal 会留在系统托盘运行。快捷键、蓝牙配对、企业 Wi-Fi、
日志位置和源码构建说明见 [`docs/WINDOWS.md`](docs/WINDOWS.md)。

## 设备按键

| 操作 | 功能 |
| --- | --- |
| 按住 G0/BtnA | 按住期间说话 |
| 双击 G0 | 锁定麦克风；再按一次结束 |
| `Fn+Space` | 键盘上的按住说话 |
| `Fn+Enter` | 开关语音结束 1400 ms 后自动发送 Enter |
| `Fn+Tab` | 切换本地按键和电脑键盘转发 |
| 任务页 Left / Right | 浏览最近八个 Codex 任务 |
| 任务页 Enter | 清除本机的完成或阻塞提醒 |

电脑确认麦克风切换和语音快捷键均已启动后，麦克风图标才会变绿。键盘图标显示
当前是否选中了电脑转发，以及连接是否可用。

## 数据路径

```text
Cardputer 麦克风和按键
        │
        ├─ Wi-Fi：TCP 控制 + UDP PCM16 音频
        └─ BLE：GATT 控制 + IMA ADPCM 音频
        │
        ▼
Windows 上的 PanPal
        ├─ 写入虚拟麦克风
        ├─ 注入按键与语音快捷键
        └─ 发送经过裁剪的 Codex 任务状态
```

首次配对会生成随机 token，后续连接用它完成身份验证。Wi-Fi 两端重启后会重新
连接保存的电脑；蓝牙完成 bonding 后，Windows 程序会扫描广播并恢复连接。

## 从源码构建

Windows PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

只编译固件：

```sh
pio run
```

`version.json` 保存程序、Bridge、固件和协议版本。修改后运行
`python tools/generate_versions.py` 更新生成文件。

## 隐私与许可证

PanPal 会向 Cardputer 发送任务名称和简短状态。提示词、会话正文、推理内容、
原始命令、工具参数、工具输出、Wi-Fi 密码和配对 token 不进入设备状态通道，
正常日志也不会记录这些内容。

项目使用 MIT License。企业 Wi-Fi supplicant 和视觉素材有各自的许可证与声明。
分发前请阅读
[`NOTICE.md`](NOTICE.md)、[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)
和 [`assets/ASSET_SOURCES.md`](assets/ASSET_SOURCES.md)。

文档入口在 [`docs/README.md`](docs/README.md)。安全报告与贡献说明见
[`SECURITY.md`](SECURITY.md) 和 [`CONTRIBUTING.md`](CONTRIBUTING.md)。

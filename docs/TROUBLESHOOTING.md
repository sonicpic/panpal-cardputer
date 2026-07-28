# Windows 故障排查

先查看 `%LOCALAPPDATA%\CodexDeck\logs\bridge.log`。提交日志前删除个人路径、
Wi-Fi 名称、Codex 内容和录音信息。

## 程序启动后反复闪黑窗

安装当前版本并检查任务管理器中是否有多份 `CardBridge.exe`。安装程序会清理旧的
CodexDeck/PanPal 启动项，只保留当前用户的一份登录启动配置。

## 托盘图标为红色

打开设置窗口查看错误文本。常见原因是 TCP 7788 被旧进程占用、VB-CABLE 未安装，
或 Windows 蓝牙服务尚未启动。退出所有旧实例，再从开始菜单启动 PanPal。

## 找不到虚拟麦克风

在 Windows 声音设置中确认存在 `CABLE Input` 和 `CABLE Output`。安装 VB-CABLE 后
需要重启系统。PanPal 设置中的音频写入设备选 `CABLE Input`，临时默认麦克风选
`CABLE Output`。

## G0 有录音图标，电脑没有反应

检查 PanPal 设置中的语音快捷键和触发模式。按住模式要求语音软件支持按下、抬起；
切换模式会在开始和结束时各点按一次快捷键。麦克风图标变绿后，Bridge 才确认了
音频切换和快捷键注入。

## Wi-Fi 连上后找不到电脑

- 确认电脑和 Cardputer 在同一个可互访的局域网；
- 检查 Windows 防火墙中的 PanPal TCP 7788、UDP 7789 规则；
- 校园网禁止终端互访时改用 Bluetooth；
- 配对完成后保留电脑端和 Cardputer 中的设备记录。

## Wi-Fi 重启后不能恢复 PC 连接

串口应先出现 `TCP connected`，随后出现 token 认证成功。持续出现
`hello send failed` 通常说明固件版本低于 0.6.5。更新配套固件后再测试。

## 蓝牙一连一断

确认固件为 0.6.5，Windows 端为 1.4.6。保持 PanPal 运行，由程序扫描和连接设备。
旧固件留下的无效 GATT 缓存会由 Bridge 重建。只有应用 token 或固件 NVS 被删除时
才需要重新输入六位码。

## Codex 一直显示 STANDBY

设置页应能看到最近任务。任务列表为空时检查 `codex.exe` 是否能从 VS Code 扩展、
ChatGPT 桌面端或 PATH 中找到。列表存在但细节状态不足时，可以安装 Hooks 并重启
Codex。rollout 生命周期会继续提供运行与完成状态。

## 源码构建失败

重新运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

如果 Inno Setup 缺失：

```powershell
winget install JRSoftware.InnoSetup
```

# Windows 安装与使用

PanPal 由 Windows 托盘程序和 Cardputer ADV 固件组成。电脑端接收按键、语音和
设备状态；Cardputer 负责采集麦克风、显示 Dapan 桌宠与 Codex 任务状态。

当前配套版本：

```text
PanPal 1.4.6 build 29
Firmware 0.6.5 build 26
Protocol 2.4
```

## 安装

1. 安装 [VB-CABLE](https://vb-audio.com/Cable/)，然后重启 Windows。
2. 运行 `PanPal-1.4.6-setup.exe`。安装程序会添加登录启动项，并为 Wi-Fi
   模式放行 TCP 7788 和 UDP 7789。
3. 打开 PanPal 设置，将音频写入设备选为 `CABLE Input`，临时默认麦克风选为
   `CABLE Output`。
4. 给 Cardputer ADV 刷入 `panpal-dapan-0.6.5-build26.bin`。
5. 在 Cardputer 上选择 Wi-Fi 或 Bluetooth，进入
   **Computers > Add computer** 完成配对。

关闭设置窗口后，程序继续在系统托盘运行。绿色图标表示设备已连接，黄色表示
等待连接或配置未完成，红色表示 Bridge 启动失败。

## Wi-Fi 与蓝牙

Cardputer 每次只初始化一种连接方式。修改连接方式后，设备保存设置并重启。

Wi-Fi 模式使用 mDNS 找到电脑，通过 TCP 7788 发送控制消息，通过 UDP 7789
传输 16 kHz PCM16 音频。电脑和固件重启后会使用已保存的 token 自动恢复连接。

Bluetooth 模式使用 BLE GATT 传输控制消息，麦克风音频使用 IMA ADPCM。首次
配对会保存应用 token 和 Windows bond。后续由 PanPal 扫描 Cardputer 广播并恢复
连接，电脑或设备重启后无需再次输入配对码。

重新配对只用于以下情况：

- Cardputer 中删除了电脑记录；
- 固件 NVS 被清除；
- `%APPDATA%\CodexDeck` 中的配对数据被删除；
- Windows 用户发生变化。

## 语音输入

按住 G0/BtnA 或 `Fn+Space` 开始说话，松开后结束。双击 G0 会锁定麦克风，适合
较长的口述；再按一次 G0 即可结束。

开始说话时，PanPal 依次完成下面的操作：

1. 记住当前默认麦克风；
2. 将默认麦克风切换为 `CABLE Output`；
3. 按下或点按用户配置的语音快捷键；
4. 将 Cardputer 音频写入 `CABLE Input`。

结束说话时，PanPal 恢复原麦克风并结束快捷键。`Fn+Enter` 控制自动回车开关。
开启后，Bridge 会在语音结束 1400 ms 后发送 Enter；这段时间内开始下一轮语音，
待发送的 Enter 会取消。

麦克风图标在电脑确认音频切换和快捷键均已执行后变绿。`Fn+Tab` 只切换键盘
转发状态，与网络连接无关。

## Codex 状态

本地会话监控始终开启。PanPal 通过 Codex App Server 每三秒读取最近八个任务，
覆盖 ChatGPT 桌面端、VS Code Codex 扩展和 Codex CLI；同时读取 rollout 文件中
的生命周期字段，判断运行、完成和停止状态。

“启用 Hooks 增强检测”只控制额外的即时事件。开启后可以补充工具调用、等待授权
和等待输入等状态；停用后，任务列表以及基本的运行和完成状态仍会更新。界面会分别
显示本地会话监控与 Hooks 的连接、启用和事件接收状态。

### 自定义额度来源

默认额度来自本机 Codex App Server：PanPal 每 30 秒调用 `account/read` 和
`account/rateLimits/read`。使用中转站账号时，可在“额度”标签中将来源改成
“自定义 URL”。PanPal 每 2 分钟发送一次只读 GET 请求；如果设置了密钥，请求头为：

```text
Authorization: Bearer <只读密钥>
```

接口返回格式：

```json
{
  "updated_at": "2026-07-28T10:30:00+08:00",
  "accounts": [
    {
      "label": "codex-1",
      "plan": "plus",
      "limits": [
        {
          "window": "weekly",
          "used_percent": 54,
          "remaining_percent": 46,
          "reset_at": "2026-08-03T00:14:00+08:00"
        }
      ]
    }
  ]
}
```

“账户标签”对应 `accounts[].label`。留空时，PanPal 会对所有带有 `weekly` 窗口的
账户计算剩余百分比平均值；缺少 `weekly` 的账户不计入分母。当前固件仍保留 5H
显示，因此自定义来源会把同一个每周平均值同时写入 5H 和 WEEKLY。5H 在这条链路中
只用于兼容，不代表真实的五小时额度。

接口必须使用 HTTPS；
本机调试允许 `http://localhost` 或回环 IP。PanPal 不跟随重定向，响应上限为
64 KiB，请求超时为 10 秒。临时请求失败时会继续显示上一次成功数据，并在电脑端
标记为过期。

中转站接口应使用独立的额度只读密钥，并在服务端缓存 30–60 秒。接口不要返回
OAuth Token、管理密钥、邮箱或通用代理能力，也不要提供额度重置操作。

发送到 Cardputer 的内容包括任务标题、项目名和简短状态。会话正文、提示词、
推理、原始命令、工具参数及工具输出不会进入设备状态消息。

## 企业 Wi-Fi

固件支持 EAP-PEAP/MSCHAPv2，连接时输入用户名和密码。校园网使用较旧 RADIUS
服务器时，固件可以协商 TLS 1.0 至 TLS 1.2。

当前固件没有配置 RADIUS CA 证书，无法验证认证服务器身份。不要在无法确认的
同名热点上输入校园网凭据。

## 日志与配置

```text
日志          %LOCALAPPDATA%\CodexDeck\logs\bridge.log
普通设置      %APPDATA%\CodexDeck\config.json
配对 token    %APPDATA%\CodexDeck\pairing-secrets\*.dpapi
额度只读密钥  %APPDATA%\CodexDeck\quota-secrets\*.dpapi
```

配对 token 和额度只读密钥使用当前 Windows 用户的 DPAPI 加密。提交问题时请删除日志中的个人
路径、Wi-Fi 名称和 Codex 内容，也不要上传录音。

无按键注入、无音频输出的诊断命令：

```powershell
.\CardBridge.exe --dry-run --no-audio -v
```

## 从源码构建

安装 Python 3.10 或更高版本、Inno Setup 6，然后在仓库根目录运行：

```powershell
winget install JRSoftware.InnoSetup
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

构建脚本会创建隔离环境、运行测试、打包托盘程序、编译固件、生成安装包并写出
SHA-256。产物位于：

```text
dist/installer/PanPal-1.4.6-setup.exe
dist/firmware/panpal-dapan-0.6.5-build26.bin
dist/firmware/panpal-dapan.bin
dist/SHA256SUMS.txt
```

安装包目前没有 Windows 代码签名，SmartScreen 会显示未知发布者。正式分发时需
使用项目自己的代码签名证书。

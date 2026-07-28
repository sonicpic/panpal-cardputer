# Windows 开发说明

## 环境

- Windows 10 或 Windows 11，x64；
- Python 3.10 或更高版本；
- Inno Setup 6；
- 支持 ESP32-S3 的 USB 数据线；
- 真机语音测试需要 VB-CABLE；
- 蓝牙测试需要可用的 Windows Bluetooth LE 适配器。

安装 Inno Setup：

```powershell
winget install JRSoftware.InnoSetup
```

## 初始化

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bootstrap.ps1
```

脚本在 `windows/.venv-build` 创建隔离环境，并安装 Bridge、PyInstaller 和
PlatformIO。它不会修改系统 Python 包。

## 测试

```powershell
$env:PYTHONPATH = "$PWD\bridge"
.\windows\.venv-build\Scripts\python.exe -m unittest discover -s bridge\tests -v
.\windows\.venv-build\Scripts\python.exe tools\generate_versions.py --check
.\windows\.venv-build\Scripts\python.exe tools\check_project.py
git diff --check
```

编译固件：

```powershell
.\windows\.venv-build\Scripts\pio.exe run
```

## 完整构建

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

可用参数：

```text
-SkipBootstrap   使用已有的 windows/.venv-build
-SkipFirmware    跳过固件编译
-SkipInstaller   跳过 Inno Setup
```

完整构建会生成安装程序、两个同内容的固件文件和 `SHA256SUMS.txt`。

## 版本文件

`version.json` 是版本源。修改后运行：

```powershell
.\windows\.venv-build\Scripts\python.exe tools\generate_versions.py
.\windows\.venv-build\Scripts\python.exe tools\generate_versions.py --check
```

下面三个文件由脚本生成：

```text
bridge/cardbridge/_generated_version.py
src/generated_version.h
release/compatibility.json
```

## 真机检查

固件变更至少检查串口启动、Wi-Fi 重连、BLE 重连和 G0 语音。语音测试还要确认：

- `CABLE Input` 有实时电平；
- 默认麦克风切换后能恢复；
- 按住与双击锁定均能结束；
- 自动 Enter 在语音结束 1400 ms 后触发；
- 新一轮语音会取消上一轮待发送的 Enter。

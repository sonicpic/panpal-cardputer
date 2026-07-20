# Codex Deck

> **Codex Deck** is an independent pocket hardware companion for OpenAI Codex,
> turning the M5Stack Cardputer ADV into a wireless keyboard, microphone, and
> live agent-status display for macOS.

> **Codex Deck** 是一个独立开源的 OpenAI Codex 便携硬件伴侣，把
> M5Stack Cardputer ADV 变成 macOS 上的无线键盘、麦克风和实时 Agent
> 状态屏。

Codex Deck is not an official OpenAI product and is not endorsed by OpenAI.
Codex Deck 不是 OpenAI 官方产品，也不代表 OpenAI 官方背书。

The current 1.x macOS package and bridge retain the internal `CardBridge`
compatibility name so existing pairings, permissions, and audio settings can
upgrade without being recreated. 当前 1.x macOS 安装包和桥接服务仍保留
`CardBridge` 技术兼容名，以便已有配对、权限和音频设置平滑升级。

## 文档 / Documentation

中文和 English installation/development guides start at
[`docs/README.md`](docs/README.md). 中英文安装与开发指南从
[`docs/README.md`](docs/README.md) 开始；产品要求和历史验收记录仍保存在
`docs/` 中，但不作为 canonical install path（规范安装入口）。

Repository slug: `cardputer-codex-deck`  |  GitHub 仓库名：`cardputer-codex-deck`

## 安装发布版 / Install a release

**中文**

普通用户应从 GitHub Releases 下载已签名/公证的 Codex Deck App 和匹配的
固件。校验 `SHA256SUMS`，把 `CardBridge.app` 放入 `/Applications`，启动
App 并按提示完成一次性的 macOS 权限设置。完整流程见
[`docs/INSTALL.md`](docs/INSTALL.md)。

从源码目录运行 `./scripts/install-release.sh`，脚本会自动下载、校验和挂载
DMG，然后安装 App。

当前预构建目标是 Apple Silicon + macOS 13 或更高版本。Codex Deck 需要
Cardputer ADV 连接 2.4 GHz Wi-Fi。安装 Mac App 和刷写 Cardputer 固件是
两个独立操作。

**English**

Download the signed/notarized Codex Deck App and matching firmware from GitHub
Releases. Verify `SHA256SUMS`, move `CardBridge.app` to `/Applications`, launch
it, and follow the one-time macOS permission prompts. See
[`docs/INSTALL.md`](docs/INSTALL.md) for the complete flow.

From a checkout, `./scripts/install-release.sh` downloads the release, verifies
its checksum, mounts the DMG, and installs the App automatically.

The packaged target is Apple Silicon on macOS 13 or newer. Codex Deck requires a
Cardputer ADV on a 2.4 GHz Wi-Fi network. Installing the Mac App and flashing
the Cardputer firmware are separate operations.

## 使用 Codex Deck macOS 菜单栏 App / Use the macOS menu bar App

**中文**

`CardBridge.app` 是当前 1.x 的内部 bundle 名称，对外显示为 Codex Deck。
它自带签名的 Bridge Agent，启动后会立即开始桥接，只显示在菜单栏，能够
自动重新连接已配对的 M5 设备，不需要 Python、虚拟环境或终端。

在 Apple Silicon 上从源码构建：

**English**

`CardBridge.app` is the internal bundle name for the current 1.x line and is
displayed publicly as Codex Deck. It bundles its signed Bridge Agent, starts
bridging immediately, appears only in the menu bar, reconnects paired M5 devices
automatically, and does not require Python, a virtual environment, or a terminal.

Build from source on Apple Silicon:

```sh
./scripts/doctor.sh
./scripts/bootstrap.sh
./scripts/test.sh
./scripts/build.sh
./scripts/install.sh
./scripts/healthcheck.sh
```

**中文**

首次启动时，Codex Deck（`CardBridge.app`）会用一次 macOS 管理员授权安装
内置的 `CardBridge Microphone` HAL 驱动，然后请求 **系统设置 → 隐私与安全性
→ 辅助功能** 权限，以便转发键盘输入。麦克风会提供一个仅输入的兼容 Core
Audio 设备和一个供 Agent 使用的仅输出音频流；如果已安装 BlackHole 2ch，
它仍可作为备用方案。已有的 `~/.cardbridge` 身份和配对数据会原地迁移，
无需重新配对；配对密钥会保存到 macOS 钥匙串。

菜单栏会显示 M5、协议、本地网络、辅助功能、音频和 Codex 健康状态。设置
页面可以管理登录启动、音频增益、已配对设备、Codex Hooks、自动更新和脱敏
诊断信息。

自动化 Agent 可以运行上面的命令，但 macOS 请求管理员、辅助功能、本地网络、
钥匙串或 Codex Hook 信任时，必须暂停并等待用户明确批准。详见
[`AGENTS.md`](AGENTS.md) 和机器可读的 [`project-install.json`](project-install.json)。

**English**

On first launch, Codex Deck (`CardBridge.app`) offers to install its bundled
`CardBridge Microphone` HAL driver with one macOS administrator prompt, then
requests **System Settings → Privacy & Security → Accessibility** for keyboard
forwarding. The microphone publishes an input-only USB-compatible Core Audio
device and a separate output-only feed used by the Agent; an existing BlackHole
2ch installation remains a fallback. Existing `~/.cardbridge` identity and
pairing data are migrated without re-pairing, and pairing secrets move to the
macOS Keychain.

The menu shows live M5, protocol, local-network, Accessibility, audio, and Codex
health. Settings manages login launch, audio gain, paired devices, Codex Hooks,
automatic updates, and redacted diagnostics.

An automation agent may run every command above, but it must pause for explicit
user approval when macOS requests administrator, Accessibility, Local Network,
Keychain, or Codex Hook trust. See [`AGENTS.md`](AGENTS.md) and the machine-readable
[`project-install.json`](project-install.json).

## 构建固件 / Build firmware

**中文**

```sh
cd /path/to/cardputer-codex-deck
pio run
```

如果 `pio` 不在 `PATH` 中，请先安装 PlatformIO Core。连接硬件后，Codex 可以
运行 `pio run -t upload` 并使用 USB 串口完成设备验证。尽量让 PlatformIO
自动发现 `/dev/cu.usbmodem*`，因为设备重置后串口名称可能变化。

**English**

```sh
cd /path/to/cardputer-codex-deck
pio run
```

Install PlatformIO Core first if `pio` is not already on `PATH`. When hardware is
connected, Codex may run `pio run -t upload`, use the USB serial port, and perform
physical-device validation. Let PlatformIO auto-detect `/dev/cu.usbmodem*` because
the port name can change after a reset.

## 版本与协议 / Release and protocol versions

**中文**

[`version.json`](version.json) 是 Mac App、Python Agent、固件、本地 Agent API、
设备协议、配置 schema 和能力列表的单一版本源。修改后运行以下命令重新生成
各语言常量：

**English**

[`version.json`](version.json) is the single version source for the Mac App,
Python Agent, firmware, local Agent API, device protocol, configuration schema,
and capability list. Regenerate language-specific constants after changing it:

```sh
python3 tools/generate_versions.py
```

CI 和本地验证应使用 `python3 tools/generate_versions.py --check`，以拒绝过期的
Python、C++ 或 Swift 常量。协议主版本不匹配会返回明确的 `upgrade_required`；
缺失的协议字段在迁移期间仍按旧协议 v1 兼容处理。

CI and local validation should use `python3 tools/generate_versions.py --check`
to reject stale generated Python, C++, or Swift constants. A protocol-major
mismatch produces an explicit `upgrade_required` response; missing protocol
fields remain compatible as legacy protocol v1 during migration.

运行完整的本地发布门禁：

Run the complete local release gate with:

```sh
CODE_SIGN_IDENTITY="Apple Development: …" macos/scripts/release.sh
```

它会测试 Swift/Python、构建固件、打包并验证 App/Agent、签名 Sparkle 归档，
并在 `macos/dist/release-<version>/` 下写入校验和与发布清单。公开分发还需要
Developer ID Application 证书和 Apple 公证凭据，详见 [`release/README.md`](release/README.md)。

It tests Swift/Python, builds firmware, packages and validates the App/Agent,
signs the Sparkle archive, and writes checksums plus release manifests under
`macos/dist/release-<version>/`. Public distribution additionally requires a
Developer ID Application certificate and Apple notarization credentials; see
[`release/README.md`](release/README.md).

## 构建宠物动画资源 / Build pet animation assets

**中文**

固件自带一个确定性的 Codex 主题开发吉祥物。使用随附的离线打包器重建：

**English**

The firmware ships with a deterministic Codex-themed development mascot. Rebuild
it with the bundled offline packer:

```sh
python3 tools/pack_pet.py --demo --output-dir src
```

如需使用官方 `hatch-pet` 流程创建的桌面 Codex v2 宠物：

To use a desktop Codex v2 pet created by the official `hatch-pet` workflow:

```sh
python3 tools/pack_pet.py \
  --pet-dir "$HOME/.codex/pets/my-pet" \
  --output-dir src
```

适配器支持 1536×1872 的 8×9 App 图集和 1536×2288 的 8×11 v2 图集，只选择
Idle、Failed、Waiting、Running 和 Review 状态，将帧缩放到 72×72，共享 16 色
调色板量化，并写入 `src/pet_assets.*` 中的行安全 RLE。Cardputer 直接从闪存
解码，在 Codex 详情页缩放到 100×100，每帧不分配图像缓冲区。

The adapter accepts both the 1536×1872 8×9 App atlas and the 1536×2288 8×11 v2
atlas. It selects Idle, Failed, Waiting, Running, and Review; packs frames at
72×72; quantizes them to a shared 16-colour palette; and writes row-safe RLE into
`src/pet_assets.*`. The Cardputer decodes those runs directly from flash, scales
them to 100×100 on the Codex detail page, and allocates no per-frame image buffer.

## 构建中文 UI 字体 / Build the Chinese UI font

**中文**

生成的 `assets/fonts/cardbridge-ui-13.bff` 内置了由 Source Han Sans CN Medium
2.005R 派生的原生 13px、4-bit 抗锯齿 GB2312 字体。原生尺寸保持小屏字宽均匀，
并不是将 15px 字体做分数缩放。运行以下命令重建：

**English**

The generated `assets/fonts/cardbridge-ui-13.bff` embeds a native 13px, 4-bit
anti-aliased GB2312 font derived from Source Han Sans CN Medium 2.005R. The native
size keeps small-screen glyph advances even; it is not a fractionally scaled 15px
face. Rebuild it with:

```sh
python3 tools/build_ui_font.py
```

生成器会验证固定的源字体 checksum，并通过 `npx` 调用 `lv_font_conv` 1.5.3。
Source Han Sans 使用 SIL Open Font License 1.1 发布，所需声明在
`assets/fonts/LICENSE-SourceHanSans.txt`。

The generator verifies the pinned source-font checksum and invokes `lv_font_conv`
1.5.3 through `npx`. Source Han Sans is distributed under the SIL Open Font
License 1.1; the required notice is in `assets/fonts/LICENSE-SourceHanSans.txt`.

## 设备控制 / Device controls

**中文**

- BtnA 切换键盘转发。状态栏最左侧的键盘图标表示转发是否开启；切换不会改变当前页面。
- 键盘转发开启时，`Fn+;`、`Fn+,`、`Fn+.`、`Fn+/` 发送上、左、下、右；`Fn+\`` 发送 Escape。Shift 会作为 macOS 修饰键附加到目标键，Ctrl/Cmd/Option 保持正常的按下/抬起事件。
- 键盘转发关闭时，可用印刷的方向键（`; . , /`）或 `I/J/K/L` 导航，`Enter` 确认，反引号/ESC 返回。
- 在 Codex 页面中，左右键切换当前显示的会话，`Enter` 将该会话的仅限 Cardputer 的完成/阻塞提醒标记为已读。收到新的用户提示后，宠物会自动回到该会话。
- 在 Wi-Fi 和已配对 Mac 列表中，`Backspace` 忘记或删除选中的保存项，不需要 Fn 组合键。
- 密码输入保留大小写和 Shift 符号：输入大写字母或符号时按住 `Shift`；`Backspace` 编辑，反引号/ESC 取消。
- Wi-Fi 设置总是从扫描列表开始，只需输入密码。

**English**

- BtnA toggles keyboard forwarding. The keyboard icon at the far left of the status bar shows whether forwarding is on; toggling it never changes the current page.
- With keyboard forwarding on, `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` send Up, Left, Down, and Right; `Fn+\`` sends Escape. Shift is attached to the target key as a macOS modifier; Ctrl/Cmd/Option retain normal down/up events.
- With keyboard forwarding off, use the printed arrow keys (`; . , /`) or `I/J/K/L` to navigate, `Enter` to confirm, and the backtick/ESC key to go back.
- On the Codex page, left/right changes the displayed session and `Enter` marks its Cardputer-only completion/blocked reminder as seen. A newer user prompt automatically moves the pet back to that session.
- In Wi-Fi and paired-Mac lists, `Backspace` forgets or deletes the selected saved item. No Fn chord is required.
- Password entry preserves case and shifted symbols: hold `Shift` while typing uppercase letters or symbols. `Backspace` edits and the backtick/ESC key cancels.
- Wi-Fi setup always starts from a scan list; only the password is typed.

## 项目政策 / Project policy

**中文**

主项目使用 MIT License。`driver/` 中的 BlackHole 派生音频驱动使用 GPLv3，
并保留自己的许可证和声明。重新分发前请阅读 [`NOTICE.md`](NOTICE.md)、
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) 和
[`assets/ASSET_SOURCES.md`](assets/ASSET_SOURCES.md)。贡献、安全报告和支持
请求说明见 [`CONTRIBUTING.md`](CONTRIBUTING.md)、[`SECURITY.md`](SECURITY.md)
和 [`SUPPORT.md`](SUPPORT.md)。

**English**

The main project is available under the MIT License. The BlackHole-derived audio
driver in `driver/` is GPLv3 and retains its own license and notices. Before
redistributing, read [`NOTICE.md`](NOTICE.md), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md),
and [`assets/ASSET_SOURCES.md`](assets/ASSET_SOURCES.md). Contributions, security
reports, and support requests are described in [`CONTRIBUTING.md`](CONTRIBUTING.md),
[`SECURITY.md`](SECURITY.md), and [`SUPPORT.md`](SUPPORT.md).

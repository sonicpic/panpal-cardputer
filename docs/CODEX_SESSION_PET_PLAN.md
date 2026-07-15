# Codex 会话管理与宠物动画规划书

> 版本：v0.2（首版实现稿）
> 日期：2026-07-15
> 范围：先完成 Codex；Claude Agent 后续复用同一套设备协议和 UI 框架。

## 1. 结论先行

Cardputer 的 Codex 页面采用单屏“会话标题 + RPG 宠物 HUD + 当前任务状态”的结构。宠物默认跟随用户最近提交 Prompt 的会话；左右键可以临时切换其他活跃会话，而其他会话的高优先级提醒只形成徽标，不强制抢走宠物焦点。

第一版只做查看与提醒，不在 Cardputer 上直接回复、批准命令、终止任务或归档任务。原因不是这些动作完全做不到，而是批准和中断属于高风险控制，应该在状态链路稳定后单独设计确认流程。

Mac 端采用混合采集方案：

1. 通过官方 Codex App Server 的 `thread/list` 获取会话 ID、名称、项目目录和更新时间。
2. 通过官方 Codex Hooks 获取当前桌面任务的实时生命周期事件。
3. CardBridge 把两路信息按 `session_id` 合并，只向 Cardputer 发送经过裁剪的状态数据。

不直接解析完整 transcript，不读取 `auth.json`，也不把 App Server 端口暴露到局域网。

## 2. 官方能力与本机验证

### 2.1 Codex 的官方对象模型

Codex App Server 把一次会话称为 Thread；每次用户请求及其后续工作是 Turn；消息、命令、工具调用等是 Item。官方接口支持创建、恢复、分叉和列出 Thread，以及启动、引导和中断 Turn，并通过通知流返回过程事件。[Codex App Server 文档](https://learn.chatgpt.com/docs/app-server)

当前本机 Codex 0.144.2 生成的协议中，Thread 运行态包括：

- `notLoaded`
- `idle`
- `active`
- `systemError`

`active` 还可能带有 `waitingOnApproval` 或 `waitingOnUserInput` 标记。

### 2.2 宠物的官方状态语义

Codex Pets 官方定义四种状态：Running、Needs input、Ready、Blocked；多任务同时有活动时，优先级是 Needs input > Blocked > Ready > Running。[Codex Pets 文档](https://learn.chatgpt.com/docs/pets)

Cardputer 沿用这四种语义，另外增加两个设备状态：

- `idle`：没有需要关注的 Codex 活动。
- `offline`：CardBridge 或 Codex 监控器不可用。

### 2.3 已完成的只读验证

本机验证结果：

- 桌面 App 正在运行自己的 App Server，传输方式是私有 stdio。
- 另起一个同版本 App Server 可以正常 `initialize` 和 `thread/list`，能看到当前会话的 ID、目录、标题预览和更新时间。
- 同一个当前任务在第二个 App Server 中显示为 `notLoaded`，不会暴露桌面 App 那条连接里的实时运行态。
- 因此，第二个 App Server 适合做“会话目录”，不能单独承担桌面任务实时监控。
- 官方 Hooks 会在 SessionStart、UserPromptSubmit、PermissionRequest、Stop 等生命周期事件中向本地命令传入 `session_id`、`turn_id`、`cwd` 等字段，适合作为桌面实时事件来源。[Codex Hooks 文档](https://learn.chatgpt.com/docs/hooks)

结论：不依赖私有 IPC，不直接读状态数据库，采用“App Server 元数据 + Hooks 事件”的组合。

## 3. 产品范围

### 3.1 第一版包含

- Codex 宠物常驻主视图。
- 当前选中会话状态：运行中、需要操作、已完成未查看、阻塞、空闲、离线。
- 最近会话列表，最多缓存 8 个。
- 会话标题、项目名、状态、最近更新时间。
- 左右切换需要关注的会话。
- 在 Cardputer 上确认“我已看到”，只清除 Cardputer 自己的未读提醒。
- 状态变化时改变宠物动作；Needs input、Ready、Blocked 可配合蜂鸣或振动能力预留提醒接口。
- 中英文会话标题显示。

### 3.2 第一版不包含

- 在 Cardputer 上输入新 Prompt 或继续对话。
- 批准/拒绝 Codex 命令。
- 中断正在运行的任务。
- 删除、归档、重命名 Codex 会话。
- 在 Cardputer 上显示完整对话、diff、终端日志或敏感命令参数。
- 动态同步桌面端当前选中的内置宠物。

这些能力可以在第二阶段逐项增加，但不能和只读监控一起一次性放开。

## 4. Cardputer 页面设计(已定稿并实现)

屏幕为 240×135，严格切成 20px + 90px + 25px 三段：

```text
┌────────────────────────────────────────┐
│⌨  WiFi   项目名 · 会话标题(超长滚动)  🔋│ 20px
├────────────────────────────────────────┤
│ HP W 40%      ┌──────────┐  MP 5H --  │
│ █████░░       │          │  ░░░░░░░   │
│               │ PET 72²  │             │ 90px
│ !1            │          │        2/4  │
│               └──────────┘             │
├────────────────────────────────────────┤
│ ● Editing project files...             │ 25px
└────────────────────────────────────────┘
```

- 顶栏只保留键盘、WiFi、电池三个图标，中间 144px 给会话标题；按实际像素宽度判断是否滚动。
- HUD 左 72px 是周限额 HP，中间 72×72 是宠物，右 72px 是 5h 限额 MP。限额条与宠物同处一行。
- 无 5h 限额时显示灰色 `MP 5H --`，不伪造百分比。
- 左右键循环切换最多 8 个会话；Enter 只清除当前会话的 CardBridge 本地提醒；ESC 返回主页。
- 新的 `UserPromptSubmit` 会把宠物跟随目标切回该会话。其他会话有 Needs input / Blocked / Ready 时以 `!N` 提醒。
- 键盘模式开启时，动画和状态照常刷新，但页面不可操作，按键全部继续发给 Mac。

## 5. 宠物形象方案

### 5.1 推荐方案

制作一只“Codex 主题的自定义宠物”，同一份高清母版同时生成：

1. Codex 桌面端可使用的自定义宠物资源。
2. Cardputer 专用的低分辨率像素动画包。

这样视觉上就是同一只宠物，而且不依赖逆向提取桌面 App 内置资源。官方支持创建自定义宠物，并说明自定义宠物资源保存在本机；网页端上传格式是 1536×1872 的透明 PNG/WebP，但这只能作为母版，不能直接在 ESP32 上运行时解码。[Codex Pets 文档](https://learn.chatgpt.com/docs/pets)

如果一定要完全复刻某一只内置宠物，需要先明确宠物名称并确认可用的原始素材；官方文档没有提供内置宠物的导出接口，因此不把“自动读取桌面当前宠物”列为第一版承诺。

### 5.2 动画状态机

| 状态 | 动作建议 | 帧数 / 帧率 | 播放方式 |
| --- | --- | --- | --- |
| Idle | 呼吸、眨眼、偶尔打盹 | 4–6 帧 / 4 fps | 循环 |
| Running | 敲键盘、尾巴摆动或能量流动 | 6–8 帧 / 8 fps | 循环 |
| Needs input | 抬头、跳一下、问号或感叹号脉冲 | 4–6 帧 / 6 fps | 循环，视觉最明显 |
| Ready | 庆祝、星星闪烁 | 6 帧 / 8 fps | 首次完整播放，随后慢循环 |
| Blocked | 卡住、冒烟或错误符号 | 4 帧 / 3 fps | 慢循环 |
| Offline | 睡眠、信号断开 | 2–4 帧 / 2 fps | 慢循环 |

链路在线时，其他会话的提醒排序采用官方优先级：

```text
Needs input > Blocked > Ready > Running > Idle
```

Offline 不参与任务优先级，它是链路覆盖状态。链路断开时直接覆盖旧任务状态，避免误导。

这套优先级只决定列表/提醒顺序，不决定宠物焦点；宠物焦点仍由最近 `UserPromptSubmit` 和设备左右切换决定。

### 5.3 图片格式与资源预算

Cardputer ADV 无 PSRAM，不在设备运行时解码大 PNG/WebP。构建时把母版转换成调色板位图或 RLE 数据，放进 Flash，逐帧直接绘制到现有 M5Canvas。

建议规格：

- 单帧：72×72。
- 色深：4-bit、16 色共享调色板；必要时个别状态使用独立调色板。
- 总帧数目标：24–30 帧。
- 未压缩资源约 62–76KB；RLE 后目标 40–80KB。
- 宠物资源硬上限：128KB Flash。
- 额外运行时堆目标：不超过 8KB；不创建第二张全屏 Canvas。
- 动画刷新：最高 8 fps，状态文字可按现有 UI 节奏刷新。

中文会话标题使用从 Source Han Sans CN Medium 生成的 15px、4-bit 抗锯齿 BFF 字库。压缩字形保存在 Flash，RAM 只保留 M5GFX 的字符定位表；缺字时仍可回退到内置字体加载失败保护。

### 5.4 实际构建链

官方 `hatch-pet` 负责桌面宠物：输入角色参考或生成图，产出所有标准动作，组装并校验 v2 `spritesheet.webp` 与 `pet.json`。CardBridge 自己的 `tools/pack_pet.py` 是第二级适配器：

```text
角色图 / 参考图
  → hatch-pet 生成标准动作
  → 1536×2288 v2 图集(8列×11行，单格192×208)
  → tools/pack_pet.py 选取 Idle/Running/Waiting/Ready/Blocked
  → 72×72 + 共享16色 + 逐行RLE
  → src/pet_assets.h/.cpp
  → ESP32 从 Flash 流式绘制到现有 M5Canvas
```

仓库自带 `--demo` 模式，离线生成 30 帧 Codex 主题开发宠物，保证没有图像服务时仍可编译和联调。接入正式桌面宠物只需：

```sh
python3 tools/pack_pet.py --pet-dir "$HOME/.codex/pets/<name>" --output-dir src
```

当前实测：155,520 字节索引像素压成 53,084 字节 RLE；整包小于 128KB 资源上限。

### 5.5 固件大小实测

当前完成版固件实测 1,471,966 字节，app 分区约 3.34MB；静态 RAM 64,780 字节。资源组成包括：

- 中文字体：约 213KB。
- 宠物动画：40–128KB。
- 会话模型、协议和页面代码：约 30–80KB。

实际占用为 app 分区 44.0%、链接器 RAM 预算 19.8%，仍有充分余量替换为正式宠物图集。

## 6. Mac 端架构

```text
Codex Desktop / CLI
   ├─ 官方 Hooks ───────────────┐
   │  start/input/tool/stop      │
   └─ 独立只读 App Server        │
      thread/list metadata       ▼
                         CodexStateReducer
                                │
                         CardBridge AgentStore
                                │ authenticated TCP
                                ▼
                            Cardputer
```

### 6.1 App Server 目录采集器

- CardBridge 以 stdio 启动本机已安装的 `codex app-server`。
- 完成 `initialize` / `initialized` 握手。
- 启动和低频刷新时调用 `thread/list`，优先设置 `useStateDbOnly: true`。
- 只保留：`id`、`name/preview`、`cwd` 最后一级和 `updatedAt`；查询时只取未归档会话。
- 不请求完整 turns，不读取消息内容。
- 启动时检测 Codex 版本；协议字段不兼容时降级为“监控不可用”，不影响键盘和音频链路。

### 6.2 Hook 实时事件采集器

仓库已提供用户级 Hook 合并安装器，但不会自动改写全局配置；安装后仍需要用户显式信任：

| Hook | 目标状态 |
| --- | --- |
| `SessionStart` | 注册/恢复会话 |
| `UserPromptSubmit` | Running |
| `PermissionRequest` | Needs input |
| `PostToolUse` | 从批准等待恢复到 Running |
| `Stop` | Ready，等待 Cardputer 本地确认 |

`waitingOnUserInput` 和系统错误要作为第一个技术 Spike 单独验证：如果桌面端 Hook 能区分，就分别映射 Needs input / Blocked；如果不能，第一版宁可显示保守的 Needs input 或 Ready，不根据提示词文本猜测。

Hook 只把最小事件通过本机 Unix datagram 或 `127.0.0.1` 发给 CardBridge：`session_id`、`turn_id`、`event`、`cwd`、时间戳。Hook 必须 fail-open：CardBridge 不在线时立即退出 0，不得阻塞 Codex，目标耗时低于 150ms。

### 6.3 状态合并

CardBridge 内部以完整 `session_id` 为主键：

- App Server 数据提供名称、项目和更新时间。
- Hook 数据覆盖实时状态。
- Ready 的未读标记由当前 CardBridge 进程保存；Cardputer 的 Seen 只清本地标记，不假装改变 Codex 桌面端的已读状态。
- 状态附带单调递增 `seq`，设备忽略倒序消息。
- CardBridge 重启后重新从 `thread/list` 建立最近会话目录，实时状态等待新的 Hook 事件恢复；第一版不把状态或未读文本写入磁盘。

## 7. CardBridge 协议草案

沿用现有已鉴权 TCP JSON 行协议，消息总长继续受 4096 字节限制。

### 7.1 请求会话快照

```json
{"t":"agent_list_req","provider":"codex","limit":8,"token":"..."}
```

### 7.2 返回会话快照

```json
{
  "t":"agent_list",
  "provider":"codex",
  "seq":42,
  "focus_id":"019f6504-afce-70c0-b317-2791d67e952d",
  "quota":{"weekly":{"remaining":40},"five_hour":null},
  "items":[
    {
      "id":"019f6504-afce-70c0-b317-2791d67e952d",
      "title":"Codex 会话与宠物规划",
      "project":"m5 cardputer",
      "status":"needs_input",
      "activity":"Waiting for your approval",
      "unread":true,
      "updated_ms":1784109000000
    }
  ],
  "token":"..."
}
```

### 7.3 推送最新完整快照

```json
{
  "t":"agent_status",
  "provider":"codex",
  "seq":43,
  "focus_id":"019f6504-afce-70c0-b317-2791d67e952d",
  "quota":{"weekly":{"remaining":40},"five_hour":null},
  "items":[{"id":"019f6504-afce-70c0-b317-2791d67e952d","status":"ready","unread":true}],
  "token":"..."
}
```

### 7.4 Cardputer 本地确认

```json
{
  "t":"agent_ack",
  "provider":"codex",
  "id":"019f6504-afce-70c0-b317-2791d67e952d",
  "token":"..."
}
```

设备连接成功后主动请求一次快照；之后以 push 为主，每 30 秒做一次轻量对账。断线仍由现有 5 秒心跳、3 次超时规则处理。

## 8. 代码模块规划

### 8.1 Mac Bridge

```text
bridge/cardbridge/
├── codex_monitor.py      # JSONL App Server + 本机 Hook UDP 接收
├── agents.py             # 状态合并、焦点、优先级、未读、限额
└── server.py             # agent_list/status/ack 接入现有 TCP

bridge/hooks/
└── cardbridge_codex.py   # fail-open 的 Hook 入口
```

### 8.2 Cardputer 固件

```text
src/
├── models.h              # 最多 8 个会话的定长模型
├── pairing.h/.cpp        # Agent JSON 解析、seq、ack
├── pet_renderer.h/.cpp  # 状态机和帧播放
├── pet_assets.h/.cpp    # PROGMEM 调色板/RLE 数据
└── ui.cpp/.h            # 单屏 Codex 宠物 HUD

tools/
└── pack_pet.py           # 构建时把母版转换为固件资源
```

固件端优先使用定长数组和有上限的字符串，避免会话列表长期运行产生堆碎片。

## 9. 实施记录

### S0：Hook 可行性 Spike（代码与本机 UDP 联调完成）

- 已提供可预览、幂等合并、可卸载的 Hook 安装器；全局安装与信任留给实机验收。
- 验证桌面 App 中的 SessionStart、UserPromptSubmit、PermissionRequest、Stop。
- 验证请求用户回答和系统错误能否被可靠区分。
- 输出真实事件样例，但不记录 Prompt、命令参数和 transcript。

通过标准：当前桌面任务从提交到 CardBridge 收到 Running 事件不超过 1 秒；完成事件不超过 1 秒；Hook 服务不可用时不影响 Codex。

### S1：只读会话监控器（完成）

- 已实现 App Server 客户端、Hook sink、状态合并和进程内未读状态。
- 完成 agent_list/status/ack 协议与 Python 单元测试。
- fake device 验证快照、推送、重连和乱序 seq。

### S2：宠物资源与固件渲染器（开发宠物完成）

- 已制作可替换的 Codex 主题开发宠物；正式桌面同款母版可后续通过 `hatch-pet` 替换。
- 已制作五组 Cardputer 动画，并以 Idle 覆盖 Offline 基础帧。
- 实现离线转换、RLE 解码和无额外全屏缓冲绘制。
- 加入中文字体并记录固件增长。

### S3：Codex 页面与联调（脱机完成，实机待验收）

- 已实现单屏宠物 HUD、左右会话切换、未读提醒和断线降级。
- 已通过固件编译、21 项桥接测试、真实 App Server 限额读取与 Hook UDP 联调。
- 按仓库分工不在开发阶段烧录；烧录与 30 分钟动画/堆稳定性由实机验收执行。

### S4：会话控制二期

按风险从低到高评估：重命名/归档、发送新 Prompt、终止 Turn、批准/拒绝。每一种控制都需要设备二次确认和 Mac 端权限边界，不和 S1–S3 混做。

## 10. 验收标准

### 状态正确性

- 运行中、等待确认、完成未读、阻塞、空闲、离线能稳定切换。
- 其他会话提醒顺序与官方 Pets 优先级一致，但不抢最近操作会话的宠物焦点。
- CardBridge 重启后不会把旧 Running 永久保留。
- Seen 只清 Cardputer 未读，不影响真实 Codex 会话。

### UI 与动画

- 宠物是 Codex 页的视觉主体，不是角落装饰。
- 中文标题可读，长标题不会挤出边界。
- 动画稳定达到 6–8 fps，连续运行 30 分钟无明显卡顿或堆下降。
- 键盘模式开启时页面不可操作，但状态和动画继续更新。
- WiFi 或 CardBridge 断开后 15 秒内进入 Offline。

### 资源

- 宠物资源不超过 128KB Flash。
- 新增运行时堆不超过 16KB。
- 固件总大小保持在 app 分区 60% 以下。
- 不引入 PSRAM 假设，不增加第二张全屏 Canvas。

### 隐私与可靠性

- CardBridge 不读取或发送 Codex 凭据。
- 默认不读取完整 transcript、Prompt、diff、终端输出。
- App Server 和 Hook sink 只监听本机；Cardputer 仍通过已有 token 鉴权。
- Hook 失败不阻塞 Codex，Agent 功能失败不影响键盘、WiFi 和现有桥接功能。

## 11. 开始制作前只需确定的一项视觉选择

推荐选择：做一只新的 Codex 主题自定义宠物，并把它同时用于桌面 Codex 和 Cardputer。

如果目标是复刻现在桌面中某一只内置宠物，则需要先提供它的名称或截图，再确认以“尽量一致的 Cardputer 像素版”实现，而不是承诺从桌面 App 自动导出。

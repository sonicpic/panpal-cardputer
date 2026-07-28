# 自定义额度接口

PanPal 可以从中转站的只读接口获取 Codex 每周额度。推荐路径：

```text
GET /v0/management/codex-quota
Authorization: Bearer <额度只读密钥>
Accept: application/json
```

响应使用 `Content-Type: application/json`，正文不超过 64 KiB：

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

字段约定：

| 字段 | 要求 |
| --- | --- |
| `updated_at` | 推荐提供，ISO 8601 时间 |
| `accounts` | 至少包含一个有效账户 |
| `label` | 账户的稳定显示名称，不要使用邮箱或 `auth_index` |
| `plan` | 可选，例如 `plus`、`pro`、`team` |
| `window` | 当前只使用 `weekly` |
| `used_percent` | 0–100，可与 `remaining_percent` 二选一 |
| `remaining_percent` | 0–100，可与 `used_percent` 二选一 |
| `reset_at` | 可选，ISO 8601 时间或 Unix 秒时间戳 |

同时返回 `used_percent` 和 `remaining_percent` 时，两者之和必须接近 100。

## 多账户计算

PanPal 的“账户标签”留空时，会读取所有带有 `weekly` 窗口的账户：

```text
平均剩余 = 所有有效 weekly.remaining_percent 之和 / 有效账户数
```

缺少 `weekly` 的账户不进入分母，最终结果四舍五入为整数。填写账户标签后，只读取
标签完全匹配的账户。

现有固件仍显示 `5H` 和 `WEEKLY`。自定义来源会把每周平均剩余同时写入这两个
字段，其中 `5H` 只承担协议兼容作用。官方 App Server 来源仍使用官方返回的两个
窗口。

## 服务端要求

- 使用独立的额度只读密钥，不接受 CPA 管理密钥。
- 在服务端缓存上游结果 30–60 秒。
- 只允许 GET，不提供额度重置或任意 URL 代理。
- 不返回 OAuth Token、邮箱、`auth_index`、ChatGPT Account ID 或原始认证文件。
- 上游暂时不可用时可以返回仍在有效期内的缓存；没有缓存时返回 503。
- 无效密钥返回 401 或 403，限流返回 429。

PanPal 每 2 分钟请求一次，超时为 10 秒，不跟随 HTTP 重定向。请求失败后继续使用
最后一次成功数据，并在 Windows 界面标记为过期。

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cardbridge.agents import AgentStore
from cardbridge.config import BridgeConfig
from cardbridge.custom_quota import (
    CustomQuotaMonitor,
    QuotaResponseError,
    normalize_custom_quota,
    validate_quota_url,
)


PAYLOAD = {
    "updated_at": "2026-07-28T10:30:00+08:00",
    "accounts": [
        {
            "label": "codex-1",
            "plan": "plus",
            "limits": [
                {
                    "window": "5h",
                    "used_percent": 25,
                    "remaining_percent": 75,
                    "reset_at": "2026-07-28T14:00:00+08:00",
                },
                {
                    "window": "weekly",
                    "used_percent": 54,
                    "remaining_percent": 46,
                    "reset_at": "2026-08-03T00:14:00+08:00",
                },
            ],
        }
    ],
}


class CustomQuotaParserTests(unittest.TestCase):
    def test_normalizes_cpa_response_for_agent_store(self) -> None:
        result, metadata = normalize_custom_quota(PAYLOAD, "codex-1")
        store = AgentStore()
        store.set_quota_mode("subscription")
        store.update_rate_limits(result)

        quota = store.snapshot()["quota"]
        self.assertEqual(quota["five_hour"]["remaining"], 46)
        self.assertEqual(quota["weekly"]["remaining"], 46)
        self.assertEqual(metadata["account_label"], "codex-1")
        self.assertEqual(metadata["plan"], "plus")

    def test_account_label_is_explicit_when_multiple_accounts_exist(self) -> None:
        with self.assertRaisesRegex(QuotaResponseError, "account label not found"):
            normalize_custom_quota(PAYLOAD, "missing")

    def test_blank_account_label_averages_weekly_and_mirrors_legacy_5h(self) -> None:
        payload = {
            "accounts": [
                {
                    "label": "a",
                    "plan": "plus",
                    "limits": [{"window": "weekly", "remaining_percent": 40}],
                },
                {
                    "label": "b",
                    "plan": "plus",
                    "limits": [{"window": "weekly", "remaining_percent": 80}],
                },
                {
                    "label": "no-weekly",
                    "plan": "plus",
                    "limits": [{"window": "5h", "remaining_percent": 0}],
                },
            ]
        }
        result, metadata = normalize_custom_quota(payload)
        store = AgentStore()
        store.set_quota_mode("subscription")
        store.update_rate_limits(result)

        quota = store.snapshot()["quota"]
        self.assertEqual(quota["five_hour"]["remaining"], 60)
        self.assertEqual(quota["weekly"]["remaining"], 60)
        self.assertEqual(metadata["account_label"], "全部 2 个账户（平均）")

    def test_rejects_disagreeing_percentages(self) -> None:
        payload = {
            "accounts": [
                {
                    "label": "bad",
                    "limits": [
                        {
                            "window": "weekly",
                            "used_percent": 20,
                            "remaining_percent": 20,
                        }
                    ],
                }
            ]
        }
        with self.assertRaisesRegex(QuotaResponseError, "disagree"):
            normalize_custom_quota(payload)

    def test_requires_https_except_for_loopback(self) -> None:
        self.assertEqual(
            validate_quota_url("https://quota.example.test/v1"),
            "https://quota.example.test/v1",
        )
        self.assertEqual(
            validate_quota_url("http://127.0.0.1:8317/quota"),
            "http://127.0.0.1:8317/quota",
        )
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            validate_quota_url("http://quota.example.test/v1")


class CustomQuotaMonitorTests(unittest.IsolatedAsyncioTestCase):
    async def test_failed_refresh_keeps_last_successful_windows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = BridgeConfig(Path(directory) / "config.json")
            config.update_quota_settings(
                {
                    "source": "custom",
                    "url": "https://quota.example.test/v1",
                }
            )
            store = AgentStore()
            monitor = CustomQuotaMonitor(store, config)
            normalized = normalize_custom_quota(PAYLOAD)
            with patch(
                "cardbridge.custom_quota.fetch_custom_quota",
                return_value=normalized,
            ):
                await monitor.refresh()
            previous = store.snapshot()["quota"]
            monitor.last_error = "temporary outage"

            self.assertEqual(store.snapshot()["quota"], previous)
            self.assertTrue(monitor.snapshot()["stale"])


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import unittest

from cardbridge.agents import AgentSession, AgentStore
from cardbridge.protocol import MAX_JSON_LINE, encode_message


class AgentStoreTests(unittest.TestCase):
    def test_focus_follows_latest_user_prompt_and_ack_clears_ready(self) -> None:
        store = AgentStore()
        store.update_threads(
            [
                {
                    "id": "session-a",
                    "name": "First task",
                    "cwd": "/tmp/project-a",
                    "updatedAt": 100,
                },
                {
                    "id": "session-b",
                    "preview": "Second task",
                    "cwd": "/tmp/project-b",
                    "updatedAt": 90,
                },
            ]
        )
        store.apply_hook_event(
            {"event": "UserPromptSubmit", "session_id": "session-b", "timestamp_ms": 1_000}
        )
        self.assertEqual(store.focus_id, "session-b")
        self.assertEqual(store.sessions["session-b"].status, "running")
        first_focus_seq = store.focus_seq
        store.apply_hook_event(
            {"event": "UserPromptSubmit", "session_id": "session-b", "timestamp_ms": 1_500}
        )
        self.assertGreater(store.focus_seq, first_focus_seq)

        store.apply_hook_event(
            {"event": "Stop", "session_id": "session-b", "timestamp_ms": 2_000}
        )
        self.assertEqual(store.sessions["session-b"].status, "ready")
        self.assertTrue(store.sessions["session-b"].unread)
        self.assertTrue(store.acknowledge("session-b"))
        self.assertEqual(store.sessions["session-b"].status, "idle")
        self.assertFalse(store.sessions["session-b"].unread)

    def test_rate_limit_windows_are_classified_by_duration(self) -> None:
        store = AgentStore()
        store.set_quota_available(True)
        store.update_rate_limits(
            {
                "rateLimitsByLimitId": {
                    "codex": {
                        "primary": {
                            "usedPercent": 55,
                            "windowDurationMins": 10_080,
                            "resetsAt": 123,
                        },
                        "secondary": None,
                    }
                }
            }
        )
        snapshot = store.snapshot()
        self.assertTrue(snapshot["quota"]["available"])
        self.assertEqual(snapshot["quota"]["weekly"]["remaining"], 45)
        self.assertIsNone(snapshot["quota"]["five_hour"])

    def test_api_mode_hides_and_clears_subscription_quota(self) -> None:
        store = AgentStore()
        store.set_quota_available(True)
        store.update_rate_limits(
            {
                "rateLimits": {
                    "primary": {
                        "usedPercent": 10,
                        "windowDurationMins": 300,
                    },
                    "secondary": {
                        "usedPercent": 20,
                        "windowDurationMins": 10_080,
                    },
                }
            }
        )
        self.assertIsNotNone(store.snapshot()["quota"]["weekly"])

        store.set_quota_available(False)
        store.update_rate_limits(
            {
                "rateLimits": {
                    "primary": {
                        "usedPercent": 99,
                        "windowDurationMins": 300,
                    }
                }
            }
        )
        quota = store.snapshot()["quota"]
        self.assertFalse(quota["available"])
        self.assertIsNone(quota["weekly"])
        self.assertIsNone(quota["five_hour"])

    def test_tool_events_produce_short_status_text(self) -> None:
        store = AgentStore()
        store.apply_hook_event(
            {
                "event": "PreToolUse",
                "session_id": "session-a",
                "tool_name": "apply_patch",
            }
        )
        self.assertEqual(store.sessions["session-a"].activity, "Editing project files...")

    def test_request_user_input_is_needs_input_until_tool_returns(self) -> None:
        store = AgentStore()
        event = {
            "event": "PreToolUse",
            "session_id": "session-a",
            "tool_name": "request_user_input",
        }
        store.apply_hook_event(event)
        self.assertEqual(store.sessions["session-a"].status, "needs_input")
        event["event"] = "PostToolUse"
        store.apply_hook_event(event)
        self.assertEqual(store.sessions["session-a"].status, "running")

    def test_focused_session_is_kept_in_bounded_snapshot(self) -> None:
        store = AgentStore()
        for index in range(10):
            store.sessions[str(index)] = AgentSession(
                id=str(index), status="needs_input", updated_ms=index
            )
        store.sessions["focus"] = AgentSession(id="focus", status="idle")
        store.focus_id = "focus"
        snapshot = store.snapshot(8)
        self.assertEqual(len(snapshot["items"]), 8)
        self.assertIn("focus", [item["id"] for item in snapshot["items"]])

    def test_worst_case_cjk_snapshot_fits_control_line(self) -> None:
        store = AgentStore()
        store.set_quota_available(True)
        store.update_rate_limits(
            {
                "rateLimits": {
                    "primary": {
                        "usedPercent": 0,
                        "windowDurationMins": 300,
                        "resetsAt": 2_147_483_647,
                    },
                    "secondary": {
                        "usedPercent": 100,
                        "windowDurationMins": 10_080,
                        "resetsAt": 2_147_483_647,
                    },
                }
            }
        )
        for index in range(8):
            session_id = str(index) * 36
            store.sessions[session_id] = AgentSession(
                id=session_id,
                title="会" * 48,
                project="项" * 24,
                status="needs_input",
                activity="Waiting for your approval",
                unread=True,
                updated_ms=index,
            )
        store.focus_id = "7" * 36
        message = store.snapshot()
        message.update(t="agent_status", provider="codex", token="ab" * 32)
        self.assertLessEqual(len(encode_message(message)), MAX_JSON_LINE)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import asyncio
import ctypes
import logging
import os
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import font as tkfont
from tkinter import messagebox, ttk
from typing import Any

from .config import BridgeConfig
from .server import BridgeApp


LOG = logging.getLogger("cardbridge.windows")

TRANSPORT_CHOICES = {
    "both": "同时监听 Wi-Fi 和蓝牙",
    "wifi": "仅监听 Wi-Fi",
    "bluetooth": "仅监听蓝牙",
}
RESTORE_CHOICES = {
    "previous": "恢复说话前的麦克风",
    "fixed": "切换到固定麦克风",
    "none": "保持虚拟麦克风",
}
TRIGGER_CHOICES = {
    "hold": "按住快捷键",
    "toggle": "按一下切换",
}
QUOTA_SOURCE_CHOICES = {
    "official": "官方 App Server",
    "custom": "自定义 URL",
}
UI_FONT_CANDIDATES = (
    "Microsoft YaHei UI",
    "Microsoft YaHei",
    "Source Han Sans SC",
    "Noto Sans CJK SC",
    "Segoe UI",
)


def display_choice(value: object, choices: dict[str, str]) -> str:
    key = str(value)
    return choices.get(key, next(iter(choices.values())))


def stored_choice(value: object, choices: dict[str, str]) -> str:
    label = str(value)
    return next(
        (key for key, display in choices.items() if display == label),
        next(iter(choices)),
    )


def choose_ui_font(families: tuple[str, ...]) -> str:
    available = {family.casefold(): family for family in families}
    for candidate in UI_FONT_CANDIDATES:
        match = available.get(candidate.casefold())
        if match:
            return match
    return "Segoe UI"


class InfoTip:
    def __init__(self, widget: tk.Widget, text: str) -> None:
        self.widget = widget
        self.text = text
        self.window: tk.Toplevel | None = None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _event: object = None) -> None:
        if self.window is not None:
            return
        window = tk.Toplevel(self.widget)
        window.wm_overrideredirect(True)
        window.wm_attributes("-topmost", True)
        x = self.widget.winfo_rootx() + self.widget.winfo_width() + 6
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 3
        window.wm_geometry(f"+{x}+{y}")
        tk.Label(
            window,
            text=self.text,
            justify="left",
            wraplength=360,
            background="#20242b",
            foreground="#ffffff",
            padx=9,
            pady=7,
            relief="solid",
            borderwidth=1,
            font="TkDefaultFont",
        ).pack()
        self.window = window

    def hide(self, _event: object = None) -> None:
        if self.window is not None:
            self.window.destroy()
            self.window = None


def info_icon(parent: tk.Misc, text: str) -> ttk.Label:
    icon = ttk.Label(parent, text="ⓘ", style="Info.TLabel", cursor="hand2")
    icon.info_tip = InfoTip(icon, text)  # type: ignore[attr-defined]
    return icon


def _configure_logging() -> None:
    local = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
    handlers: list[logging.Handler] = []
    if local:
        directory = Path(local) / "CodexDeck" / "logs"
        directory.mkdir(parents=True, exist_ok=True)
        handlers.append(logging.FileHandler(directory / "bridge.log", encoding="utf-8"))
    if not handlers:
        handlers.append(logging.NullHandler())
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        handlers=handlers,
    )


def _asset(name: str) -> Path:
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2]))
    bundled = root / "windows" / "assets" / name
    if bundled.exists():
        return bundled
    return Path(__file__).resolve().parents[2] / "windows" / "assets" / name


class BridgeRuntime:
    """Run the asyncio bridge beside Tk without spawning console windows."""

    def __init__(self, config: BridgeConfig) -> None:
        self.config = config
        self.loop: asyncio.AbstractEventLoop | None = None
        self.app: BridgeApp | None = None
        self.thread: threading.Thread | None = None
        self.ready = threading.Event()
        self.stopping = False
        self.restart_requested = False
        self.last_error = ""

    def start(self) -> None:
        if self.thread and self.thread.is_alive():
            return
        self.stopping = False
        self.thread = threading.Thread(
            target=self._thread_main, name="CodexDeckBridge", daemon=True
        )
        self.thread.start()

    def _thread_main(self) -> None:
        com_initialized = False
        try:
            if sys.platform == "win32":
                # WinRT BLE notifications need an MTA. Tk owns the main GUI
                # thread, while this dedicated asyncio thread is explicitly MTA.
                result = ctypes.windll.ole32.CoInitializeEx(None, 0)
                com_initialized = result in (0, 1)
            asyncio.run(self._run())
        except Exception as exc:  # pragma: no cover - last-resort UI reporting
            self.last_error = str(exc)
            LOG.exception("Windows bridge runtime stopped")
        finally:
            if com_initialized:
                ctypes.windll.ole32.CoUninitialize()
            self.app = None
            self.loop = None
            self.ready.set()

    async def _run(self) -> None:
        self.loop = asyncio.get_running_loop()
        while not self.stopping:
            self.restart_requested = False
            self.ready.clear()
            app = BridgeApp(config=self.config)
            self.app = app
            try:
                await app.start()
                self.last_error = ""
                self.ready.set()
                await app.shutdown_requested.wait()
            except Exception as exc:
                self.last_error = str(exc)
                LOG.exception("Windows bridge failed to start")
                self.ready.set()
            finally:
                await app.stop()
                self.app = None
            if not self.restart_requested:
                break

    async def _command(self, request: dict[str, object]) -> dict[str, object]:
        if self.app is None:
            return {"ok": False, "error": "bridge_not_running"}
        return await self.app.handle_control_command(request)

    async def _snapshot(self) -> dict[str, object]:
        if self.app is None:
            return {
                "agent": {
                    "state": "failed" if self.last_error else "starting",
                    "last_error": self.last_error,
                }
            }
        return self.app.status_snapshot()

    def command(
        self, request: dict[str, object], timeout: float = 10.0
    ) -> dict[str, object]:
        if self.loop is None:
            return {"ok": False, "error": "bridge_not_running"}
        future = asyncio.run_coroutine_threadsafe(self._command(request), self.loop)
        return future.result(timeout=timeout)

    def snapshot(self, timeout: float = 2.0) -> dict[str, object]:
        if self.loop is None:
            return {
                "agent": {
                    "state": "failed" if self.last_error else "starting",
                    "last_error": self.last_error,
                }
            }
        future = asyncio.run_coroutine_threadsafe(self._snapshot(), self.loop)
        return future.result(timeout=timeout)

    def restart(self) -> None:
        if self.thread is None or not self.thread.is_alive():
            self.start()
            return
        self.restart_requested = True
        result = self.command({"name": "restart"})
        if not result.get("ok"):
            self.last_error = str(result.get("error") or "restart failed")

    def stop(self) -> None:
        self.stopping = True
        self.restart_requested = False
        if self.loop is not None and self.app is not None:
            self.loop.call_soon_threadsafe(self.app.shutdown_requested.set)
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=8)


class CodexDeckWindow:
    def __init__(
        self, root: tk.Tk, runtime: BridgeRuntime, config: BridgeConfig
    ) -> None:
        self.root = root
        self.runtime = runtime
        self.config = config
        self.actions: queue.Queue[str] = queue.Queue()
        self.tray: Any = None
        self.tray_color = ""
        self.last_pairing_code = ""
        self.hooks_installed = False

        root.title("PanPal")
        root.protocol("WM_DELETE_WINDOW", self.hide)
        try:
            root.iconbitmap(default=str(_asset("codex-deck-green.ico")))
        except tk.TclError:
            pass

        self._configure_style()
        self._build()
        self._fit_window()
        self._load_settings()
        self._start_tray()
        root.after(200, self._poll_actions)
        root.after(400, self._poll_status)
        root.after(800, self.refresh_devices)

    def _configure_style(self) -> None:
        self.root.configure(background="#f4f6f8")
        self.ui_font_family = choose_ui_font(tuple(tkfont.families(self.root)))
        for name in (
            "TkDefaultFont",
            "TkTextFont",
            "TkMenuFont",
            "TkCaptionFont",
            "TkSmallCaptionFont",
            "TkIconFont",
            "TkTooltipFont",
        ):
            try:
                tkfont.nametofont(name, self.root).configure(
                    family=self.ui_font_family,
                    size=9,
                )
            except tk.TclError:
                pass
        style = ttk.Style(self.root)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure(".", font="TkDefaultFont")
        style.configure("App.TFrame", background="#f4f6f8")
        style.configure(
            "Title.TLabel",
            font=(self.ui_font_family, 11, "bold"),
        )
        style.configure(
            "Status.TLabel",
            font=(self.ui_font_family, 10, "bold"),
        )
        style.configure("Muted.TLabel", foreground="#667085")
        style.configure("Footer.TLabel", foreground="#667085", background="#f4f6f8")
        style.configure("Pair.TLabel", font=(self.ui_font_family, 16, "bold"))
        style.configure("Info.TLabel", foreground="#667085")
        style.configure("TNotebook.Tab", padding=(12, 5))

    def _fit_window(self) -> None:
        width = min(600, max(420, self.root.winfo_screenwidth() - 80))
        height = min(440, max(360, self.root.winfo_screenheight() - 120))
        self.root.geometry(f"{width}x{height}")
        self.root.minsize(min(520, width), min(400, height))

    def _build(self) -> None:
        shell = ttk.Frame(self.root, style="App.TFrame")
        shell.pack(fill="both", expand=True)

        top = ttk.Frame(shell, padding=(14, 10, 14, 8), style="App.TFrame")
        top.pack(fill="x")
        ttk.Label(top, text="PanPal", style="Title.TLabel").pack(anchor="w")

        status = tk.Frame(
            top,
            background="#ffffff",
            highlightbackground="#d8dde5",
            highlightthickness=1,
        )
        status.pack(fill="x", pady=(8, 0))
        self.status_accent = tk.Frame(status, width=5, background="#f2b01e")
        self.status_accent.pack(side="left", fill="y")
        status_text = tk.Frame(status, background="#ffffff", padx=10, pady=8)
        status_text.pack(side="left", fill="both", expand=True)
        self.status_label = tk.Label(
            status_text,
            text="正在启动…",
            anchor="w",
            background="#ffffff",
            foreground="#20242b",
            font=(self.ui_font_family, 9, "bold"),
        )
        self.status_label.pack(fill="x")
        self.detail_label = tk.Label(
            status_text,
            text="",
            anchor="w",
            justify="left",
            background="#ffffff",
            foreground="#667085",
            wraplength=390,
            font=(self.ui_font_family, 9),
        )
        self.detail_label.pack(fill="x", pady=(2, 0))
        pair = tk.Frame(status, background="#ffffff", padx=12, pady=7)
        pair.pack(side="right", fill="y")
        self.pair_caption = tk.Label(
            pair,
            text="",
            background="#ffffff",
            foreground="#667085",
            font=(self.ui_font_family, 8),
        )
        self.pair_caption.pack()
        self.pair_label = tk.Label(
            pair,
            text="",
            background="#ffffff",
            foreground="#20242b",
            font=("Consolas", 16, "bold"),
        )
        self.pair_label.pack()

        notebook = ttk.Notebook(shell)
        notebook.pack(fill="both", expand=True, padx=14)
        routing = ttk.Frame(notebook, padding=12)
        connection = ttk.Frame(notebook, padding=12)
        quota = ttk.Frame(notebook, padding=12)
        notebook.add(routing, text="语音设置")
        notebook.add(connection, text="连接与任务")
        notebook.add(quota, text="额度")
        routing.columnconfigure(1, weight=1)

        self.feed = ttk.Combobox(routing)
        self.virtual = ttk.Combobox(routing)
        self.restore_mode = ttk.Combobox(
            routing, values=("previous", "fixed", "none"), state="readonly"
        )
        self.restore = ttk.Combobox(routing)
        self.hotkey = ttk.Entry(routing)
        self.trigger = ttk.Combobox(
            routing, values=tuple(TRIGGER_CHOICES.values()), state="readonly"
        )
        self.restore_mode.configure(values=tuple(RESTORE_CHOICES.values()))

        fields = (
            ("音频写入设备", self.feed),
            ("临时默认麦克风", self.virtual),
            ("说完后恢复", self.restore_mode),
            ("固定恢复麦克风", self.restore),
            ("语音软件快捷键", self.hotkey),
            ("语音软件触发模式", self.trigger),
        )
        for row, (label, widget) in enumerate(fields):
            ttk.Label(routing, text=label).grid(row=row, column=0, sticky="w", pady=3)
            widget.grid(row=row, column=1, sticky="ew", padx=(12, 0), pady=3)

        connection.columnconfigure(1, weight=1)
        ttk.Label(connection, text="Cardputer 连接").grid(row=0, column=0, sticky="w")
        self.transport = ttk.Combobox(
            connection,
            values=tuple(TRANSPORT_CHOICES.values()),
            state="readonly",
        )
        self.transport.grid(row=0, column=1, sticky="ew", padx=(12, 0))
        monitor_heading = ttk.Frame(connection)
        monitor_heading.grid(row=1, column=0, sticky="nw", pady=(10, 0))
        ttk.Label(monitor_heading, text="任务与限额").pack(side="left")
        info_icon(
            monitor_heading,
            "Codex App Server 读取任务列表、标题、账号模式和官方限额；它不负责跨进程实时状态。",
        ).pack(side="left", padx=(5, 0))
        self.monitor_status_label = ttk.Label(
            connection,
            text="App Server 正在连接…",
            style="Muted.TLabel",
            wraplength=380,
        )
        self.monitor_status_label.grid(
            row=1, column=1, sticky="w", padx=(12, 0), pady=(9, 0)
        )
        rollout_heading = ttk.Frame(connection)
        rollout_heading.grid(row=2, column=0, sticky="nw", pady=(10, 0))
        ttk.Label(rollout_heading, text="运行状态").pack(side="left")
        info_icon(
            rollout_heading,
            "每 3 秒读取 rollout 文件中的生命周期字段，用来判断运行、完成和停止。",
        ).pack(side="left", padx=(5, 0))
        self.rollout_status_label = ttk.Label(
            connection,
            text="等待任务列表",
            style="Muted.TLabel",
            wraplength=380,
        )
        self.rollout_status_label.grid(
            row=2, column=1, sticky="w", padx=(12, 0), pady=(9, 0)
        )
        hooks_heading = ttk.Frame(connection)
        hooks_heading.grid(row=3, column=0, sticky="nw", pady=(10, 0))
        ttk.Label(hooks_heading, text="Hooks 增强检测").pack(side="left")
        info_icon(
            hooks_heading,
            "可选功能。补充工具调用、等待授权和等待输入等即时事件；关闭后基础会话监控仍会运行。",
        ).pack(side="left", padx=(5, 0))
        hooks = ttk.Frame(connection)
        hooks.grid(row=3, column=1, sticky="ew", padx=(12, 0), pady=(7, 0))
        self.hooks_button = ttk.Button(
            hooks,
            text="启用 Hooks 增强检测",
            command=self.toggle_hooks,
        )
        self.hooks_button.pack(anchor="w")
        self.hooks_status_label = ttk.Label(
            hooks,
            text="未启用",
            style="Muted.TLabel",
            wraplength=380,
        )
        self.hooks_status_label.pack(anchor="w", pady=(5, 0))

        quota.columnconfigure(1, weight=1)
        self.quota_source = ttk.Combobox(
            quota,
            values=tuple(QUOTA_SOURCE_CHOICES.values()),
            state="readonly",
        )
        self.quota_url = ttk.Entry(quota)
        self.quota_account = ttk.Entry(quota)
        self.quota_key = ttk.Entry(quota, show="•")
        quota_fields = (
            ("额度来源", self.quota_source),
            ("请求 URL", self.quota_url),
            ("账户标签", self.quota_account),
            ("只读密钥", self.quota_key),
        )
        for row, (label, widget) in enumerate(quota_fields):
            heading = ttk.Frame(quota)
            heading.grid(row=row, column=0, sticky="w", pady=4)
            ttk.Label(heading, text=label).pack(side="left")
            if label == "请求 URL":
                info_icon(
                    heading,
                    "只接受 HTTPS；本机调试可使用 http://localhost。请求方式为 GET。",
                ).pack(side="left", padx=(5, 0))
            elif label == "账户标签":
                info_icon(
                    heading,
                    "对应接口 accounts 数组里的 label。留空时计算所有账户的每周额度平均值。",
                ).pack(side="left", padx=(5, 0))
            elif label == "只读密钥":
                info_icon(
                    heading,
                    "通过 Authorization: Bearer 发送。留空会保留已经保存的密钥。",
                ).pack(side="left", padx=(5, 0))
            widget.grid(row=row, column=1, sticky="ew", padx=(12, 0), pady=4)
        quota_actions = ttk.Frame(quota)
        quota_actions.grid(row=4, column=1, sticky="w", padx=(12, 0), pady=(7, 0))
        ttk.Button(
            quota_actions, text="测试请求", command=self.test_quota
        ).pack(side="left")
        ttk.Button(
            quota_actions, text="清除密钥", command=self.clear_quota_key
        ).pack(side="left", padx=(7, 0))
        self.quota_key_status = ttk.Label(
            quota, text="", style="Muted.TLabel"
        )
        self.quota_key_status.grid(
            row=5, column=1, sticky="w", padx=(12, 0), pady=(7, 0)
        )
        self.quota_status_label = ttk.Label(
            quota, text="等待状态", style="Muted.TLabel", wraplength=390
        )
        self.quota_status_label.grid(
            row=6, column=0, columnspan=2, sticky="w", pady=(9, 0)
        )

        actions = ttk.Frame(shell, padding=(14, 8, 14, 10), style="App.TFrame")
        actions.pack(fill="x")
        buttons = ttk.Frame(actions, style="App.TFrame")
        buttons.pack(fill="x")
        ttk.Button(buttons, text="刷新音频设备", command=self.refresh_devices).pack(side="left")
        ttk.Button(buttons, text="保存并应用", command=self.save).pack(side="right")
        ttk.Button(buttons, text="重启后台服务", command=self.restart).pack(side="right", padx=8)

        footer = ttk.Frame(actions, style="App.TFrame")
        footer.pack(anchor="w", pady=(8, 0))
        ttk.Label(
            footer,
            text="关闭窗口后继续在系统托盘运行",
            style="Footer.TLabel",
        ).pack(side="left")
        info_icon(
            footer,
            "语音输入需要预先安装 VB-CABLE，并选择 CABLE Input 与 CABLE Output。",
        ).pack(side="left", padx=(5, 0))

    def _load_settings(self) -> None:
        settings = self.config.voice_settings()
        self.feed.set(str(settings["feed_output_device"]))
        self.virtual.set(str(settings["virtual_input_device"]))
        self.restore_mode.set(display_choice(settings["restore_mode"], RESTORE_CHOICES))
        self.restore.set(str(settings["restore_input_device"]))
        self.hotkey.insert(0, str(settings["hotkey"]))
        self.trigger.set(display_choice(settings["trigger_mode"], TRIGGER_CHOICES))
        self.transport.set(
            display_choice(self.config.bridge_transport(), TRANSPORT_CHOICES)
        )
        quota = self.config.quota_settings()
        self.quota_source.set(
            display_choice(quota["source"], QUOTA_SOURCE_CHOICES)
        )
        self.quota_url.insert(0, str(quota["url"]))
        self.quota_account.insert(0, str(quota["account_label"]))
        self.quota_key_status.configure(
            text="已安全保存密钥" if quota["api_key_present"] else "未保存密钥"
        )

    def refresh_devices(self) -> None:
        try:
            result = self.runtime.command({"name": "list_audio_devices"})
        except Exception as exc:
            messagebox.showwarning("PanPal", f"无法读取音频设备：{exc}")
            return
        if not result.get("ok"):
            messagebox.showwarning(
                "PanPal", f"无法读取音频设备：{result.get('error', '未知错误')}"
            )
            return
        playback = tuple(str(item) for item in result.get("playback", []))
        capture = tuple(str(item) for item in result.get("capture", []))
        self.feed.configure(values=playback)
        self.virtual.configure(values=capture)
        self.restore.configure(values=capture)

    def save(self) -> None:
        values: dict[str, object] = {
            "feed_output_device": self.feed.get().strip(),
            "virtual_input_device": self.virtual.get().strip(),
            "restore_mode": stored_choice(self.restore_mode.get(), RESTORE_CHOICES),
            "restore_input_device": self.restore.get().strip(),
            "hotkey": self.hotkey.get().strip(),
            "trigger_mode": stored_choice(self.trigger.get(), TRIGGER_CHOICES),
        }
        quota_values: dict[str, object] = {
            "source": stored_choice(
                self.quota_source.get(), QUOTA_SOURCE_CHOICES
            ),
            "url": self.quota_url.get().strip(),
            "account_label": self.quota_account.get().strip(),
            "api_key": self.quota_key.get().strip(),
        }
        try:
            quota_result = self.runtime.command(
                {"name": "set_quota_settings", "value": quota_values}
            )
            if not quota_result.get("ok"):
                raise RuntimeError(
                    str(quota_result.get("error") or "额度设置无效")
                )
            voice = self.runtime.command({"name": "set_voice_settings", "value": values})
            if not voice.get("ok"):
                raise RuntimeError(str(voice.get("error") or "语音设置无效"))
            selected = stored_choice(self.transport.get(), TRANSPORT_CHOICES)
            transport = self.runtime.command(
                {"name": "set_bridge_transport", "value": selected}
            )
            if not transport.get("ok"):
                raise RuntimeError(str(transport.get("error") or "连接设置无效"))
            self.runtime.restart()
        except Exception as exc:
            messagebox.showerror("PanPal", f"保存失败：{exc}")
            return
        messagebox.showinfo("PanPal", "设置已保存，Bridge 正在重启。")

    def test_quota(self) -> None:
        if stored_choice(self.quota_source.get(), QUOTA_SOURCE_CHOICES) != "custom":
            messagebox.showinfo("PanPal", "当前使用官方 App Server 额度，无需测试 URL。")
            return
        values = {
            "url": self.quota_url.get().strip(),
            "account_label": self.quota_account.get().strip(),
            "api_key": self.quota_key.get().strip(),
        }
        try:
            result = self.runtime.command(
                {"name": "test_quota_settings", "value": values}, timeout=15.0
            )
        except Exception as exc:
            messagebox.showerror("PanPal", f"额度请求失败：{exc}")
            return
        if not result.get("ok"):
            messagebox.showerror("PanPal", f"额度请求失败：{result.get('error')}")
            return
        label = str(result.get("account_label") or "默认账户")
        plan = str(result.get("plan") or "未知套餐")
        messagebox.showinfo("PanPal", f"请求成功\n账户：{label}\n套餐：{plan}")

    def clear_quota_key(self) -> None:
        if not messagebox.askyesno("PanPal", "清除已经保存的额度只读密钥？"):
            return
        result = self.runtime.command(
            {"name": "set_quota_settings", "value": {"clear_api_key": True}}
        )
        if not result.get("ok"):
            messagebox.showerror("PanPal", f"清除失败：{result.get('error')}")
            return
        self.quota_key.delete(0, tk.END)
        self.quota_key_status.configure(text="未保存密钥")

    def restart(self) -> None:
        try:
            self.runtime.restart()
        except Exception as exc:
            messagebox.showerror("PanPal", f"重启失败：{exc}")

    def toggle_hooks(self) -> None:
        action = "uninstall_hooks" if self.hooks_installed else "install_hooks"
        try:
            result = self.runtime.command({"name": action})
        except Exception as exc:
            messagebox.showerror("PanPal", f"Hooks 更新失败：{exc}")
            return
        if not result.get("ok"):
            messagebox.showerror("PanPal", f"Hooks 更新失败：{result.get('error')}")
            return
        installed = bool(result.get("hooks_installed"))
        self.hooks_installed = installed
        self.hooks_button.configure(
            text="停用 Hooks 增强检测" if installed else "启用 Hooks 增强检测"
        )
        listening = bool(result.get("hooks_listening"))
        command = str(result.get("hooks_command") or "")
        last_event = result.get("hooks_last_event_ms")
        if installed:
            message = (
                "Hooks 增强检测已启用。\n"
                f"本地接收端：{'运行中' if listening else '未运行'}\n"
                f"命令：{command}\n\n"
                + (
                    "已经收到过 Hook 事件。"
                    if last_event
                    else "尚未收到 Hook 事件。请重启 ChatGPT/VS Code 后新建或继续一次任务。"
                )
                + "\n是否出现信任提示取决于应用版本和已有信任状态，不作为安装成功的唯一判断。"
            )
        else:
            message = "Hooks 增强检测已停用。本地会话监控会继续运行。"
        messagebox.showinfo(
            "PanPal",
            message,
        )

    def _poll_status(self) -> None:
        try:
            snapshot = self.runtime.snapshot()
            self._show_status(snapshot)
        except Exception as exc:
            self._set_color("red", "Bridge 状态读取失败", str(exc))
        interval_ms = 5000 if self.root.state() == "withdrawn" else 1500
        self.root.after(interval_ms, self._poll_status)

    def _show_status(self, snapshot: dict[str, object]) -> None:
        agent = snapshot.get("agent") if isinstance(snapshot.get("agent"), dict) else {}
        state = str(agent.get("state") or "starting")
        devices = snapshot.get("devices") if isinstance(snapshot.get("devices"), list) else []
        voice = snapshot.get("voice") if isinstance(snapshot.get("voice"), dict) else {}
        codex = snapshot.get("codex") if isinstance(snapshot.get("codex"), dict) else {}
        transports = (
            snapshot.get("transports") if isinstance(snapshot.get("transports"), dict) else {}
        )
        pairing = snapshot.get("pairing") if isinstance(snapshot.get("pairing"), dict) else None

        if state in {"failed", "stopped"}:
            color = "red"
        elif state == "connected" and not agent.get("issues"):
            color = "green"
        else:
            color = "yellow"
        label = "语音输入中" if voice.get("active") else {
            "connected": "Cardputer 已连接",
            "degraded": "已连接，但需要处理配置",
            "ready": "Bridge 已运行，等待 Cardputer",
            "starting": "Bridge 正在启动",
            "failed": "Bridge 启动失败",
            "stopped": "Bridge 已停止",
        }.get(state, state)
        transport_text = str(transports.get("configured") or "both")
        detail = (
            f"设备 {len(devices)} · 监听 {transport_text} · "
            f"会话同步 {'已连接' if codex.get('connected') else '等待中'}"
        )
        error = str(
            voice.get("last_error")
            or agent.get("last_error")
            or transports.get("bluetooth_error")
            or ""
        )
        if error:
            if "virtual microphone output was not found" in error:
                detail = "未找到 CABLE Input，请安装 VB-CABLE 后刷新音频设备。"
            else:
                detail = error
        self._set_color(color, label, detail)
        codex_connected = bool(codex.get("connected"))
        executable = str(codex.get("executable") or "")
        if codex_connected:
            monitor_status = "App Server 已连接 · 任务列表"
            rollout_status = "已开启 · rollout 文件"
        elif executable:
            monitor_status = "App Server 等待连接"
            rollout_status = "等待任务列表"
        else:
            monitor_status = "未找到可用的 Codex 程序"
            rollout_status = "等待任务列表"
        self.monitor_status_label.configure(text=monitor_status)
        self.rollout_status_label.configure(text=rollout_status)

        quota = codex.get("quota") if isinstance(codex.get("quota"), dict) else {}
        quota_source = str(quota.get("source") or "official")
        if quota_source == "custom":
            if quota.get("last_error"):
                prefix = "使用上次数据 · " if quota.get("stale") else "请求失败 · "
                quota_status = prefix + str(quota["last_error"])
            elif quota.get("last_success_ms"):
                account = str(quota.get("account_label") or "默认账户")
                quota_status = f"自定义额度已更新 · {account}"
            else:
                quota_status = "自定义额度正在请求"
        elif codex.get("quota_available"):
            quota_status = "官方 App Server 额度已读取"
        else:
            quota_status = "官方额度等待中"
        self.quota_status_label.configure(text=quota_status)

        self.hooks_installed = bool(codex.get("hooks_installed"))
        self.hooks_button.configure(
            text=(
                "停用 Hooks 增强检测"
                if self.hooks_installed
                else "启用 Hooks 增强检测"
            )
        )
        if not codex.get("hooks_installed"):
            hooks_status = "未启用"
        elif not codex.get("hooks_listening"):
            hooks_status = "接收服务未运行"
        elif codex.get("hooks_last_event_ms"):
            hooks_status = f"已验证 · 最近收到 {codex.get('hooks_last_event')} 事件"
        else:
            hooks_status = "已启用 · 等待事件"
        self.hooks_status_label.configure(text=hooks_status)

        code = str(pairing.get("code") or "") if pairing else ""
        self.pair_caption.configure(text="配对码" if code else "")
        self.pair_label.configure(text=code)
        if code and code != self.last_pairing_code:
            self.last_pairing_code = code
            self.show()
            self.root.bell()

    def _set_color(self, color: str, label: str, detail: str) -> None:
        palette = {"green": "#27ae60", "yellow": "#f2b01e", "red": "#d64541"}
        self.status_accent.configure(background=palette[color])
        self.status_label.configure(text=label)
        self.detail_label.configure(text=detail)
        if self.tray is not None and color != self.tray_color:
            self.tray_color = color
            try:
                self.tray.update(
                    icon=str(_asset(f"codex-deck-{color}.ico")),
                    hover_text=f"PanPal - {label}",
                )
            except Exception:
                LOG.debug("tray update failed", exc_info=True)

    def _start_tray(self) -> None:
        try:
            from infi.systray import SysTrayIcon

            menu = (
                ("打开设置", None, lambda _tray: self.actions.put("show")),
                ("重启 Bridge", None, lambda _tray: self.actions.put("restart")),
            )
            self.tray = SysTrayIcon(
                str(_asset("codex-deck-yellow.ico")),
                "PanPal - 正在启动",
                menu,
                on_quit=lambda _tray: self.actions.put("quit"),
                default_menu_index=0,
            )
            self.tray.start()
            self.tray_color = "yellow"
        except Exception:
            self.tray = None
            LOG.exception("system tray could not start")

    def _poll_actions(self) -> None:
        while True:
            try:
                action = self.actions.get_nowait()
            except queue.Empty:
                break
            if action == "show":
                self.show()
            elif action == "restart":
                self.restart()
            elif action == "quit":
                self.quit()
                return
        self.root.after(200, self._poll_actions)

    def show(self) -> None:
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def hide(self) -> None:
        self.root.withdraw()

    def quit(self) -> None:
        if self.tray is not None:
            tray = self.tray
            self.tray = None
            threading.Thread(target=tray.shutdown, daemon=True).start()
        self.runtime.stop()
        self.root.destroy()


def _single_instance() -> object | None:
    if sys.platform != "win32":
        return object()
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.CreateMutexW(None, False, "Local\\CodexDeckWindowsBridge")
    if not handle or kernel32.GetLastError() == 183:
        return None
    return handle


def main() -> None:
    _configure_logging()
    mutex = _single_instance()
    if mutex is None:
        return
    # Create the persistent identity exactly once.  Previously the GUI and the
    # background runtime raced to create/load config.json independently.  On a
    # fresh Windows profile that could advertise bridge ID A while bridge ID B
    # won the file race, making every restart look like a new computer.
    config = BridgeConfig()
    runtime = BridgeRuntime(config)
    runtime.start()
    root = tk.Tk()
    CodexDeckWindow(root, runtime, config)
    root.mainloop()


if __name__ == "__main__":
    main()

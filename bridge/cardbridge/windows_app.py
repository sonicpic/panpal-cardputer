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
from tkinter import messagebox, ttk
from typing import Any

from .config import BridgeConfig
from .server import BridgeApp


LOG = logging.getLogger("cardbridge.windows")


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

        root.title("PanPal")
        root.geometry("620x560")
        root.minsize(580, 520)
        root.protocol("WM_DELETE_WINDOW", self.hide)
        try:
            root.iconbitmap(default=str(_asset("codex-deck-green.ico")))
        except tk.TclError:
            pass

        self._build()
        self._load_settings()
        self._start_tray()
        root.after(200, self._poll_actions)
        root.after(400, self._poll_status)
        root.after(800, self.refresh_devices)

    def _build(self) -> None:
        outer = ttk.Frame(self.root, padding=14)
        outer.pack(fill="both", expand=True)

        status = ttk.LabelFrame(outer, text="运行状态", padding=10)
        status.pack(fill="x")
        self.status_dot = tk.Canvas(status, width=18, height=18, highlightthickness=0)
        self.status_dot.grid(row=0, column=0, padx=(0, 8))
        self.status_label = ttk.Label(status, text="正在启动…")
        self.status_label.grid(row=0, column=1, sticky="w")
        self.detail_label = ttk.Label(status, text="", foreground="#666666")
        self.detail_label.grid(row=1, column=1, sticky="w")
        self.pair_label = ttk.Label(status, text="", font=("Segoe UI", 16, "bold"))
        self.pair_label.grid(row=0, column=2, rowspan=2, padx=(20, 0), sticky="e")
        status.columnconfigure(1, weight=1)

        routing = ttk.LabelFrame(outer, text="语音输入", padding=10)
        routing.pack(fill="x", pady=(10, 0))
        routing.columnconfigure(1, weight=1)

        self.feed = ttk.Combobox(routing)
        self.virtual = ttk.Combobox(routing)
        self.restore_mode = ttk.Combobox(
            routing, values=("previous", "fixed", "none"), state="readonly"
        )
        self.restore = ttk.Combobox(routing)
        self.hotkey = ttk.Entry(routing)
        self.trigger = ttk.Combobox(
            routing, values=("hold", "toggle"), state="readonly"
        )

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

        connection = ttk.LabelFrame(outer, text="连接与会话状态", padding=10)
        connection.pack(fill="x", pady=(10, 0))
        connection.columnconfigure(1, weight=1)
        ttk.Label(connection, text="Bridge 监听方式").grid(row=0, column=0, sticky="w")
        self.transport = ttk.Combobox(
            connection,
            values=("both", "wifi", "bluetooth"),
            state="readonly",
        )
        self.transport.grid(row=0, column=1, sticky="ew", padx=(12, 0))
        self.hooks_button = ttk.Button(connection, text="安装会话 Hooks", command=self.toggle_hooks)
        self.hooks_button.grid(row=1, column=1, sticky="w", padx=(12, 0), pady=(8, 0))
        ttk.Label(
            connection,
            text="Hooks 为 ChatGPT、VS Code 和 CLI 共用；相关应用会显示信任提示。",
            foreground="#666666",
        ).grid(row=2, column=0, columnspan=2, sticky="w", pady=(6, 0))
        self.hooks_status_label = ttk.Label(
            connection,
            text="Hooks 状态：正在检查…",
            foreground="#666666",
            wraplength=560,
        )
        self.hooks_status_label.grid(
            row=3, column=0, columnspan=2, sticky="w", pady=(4, 0)
        )

        buttons = ttk.Frame(outer)
        buttons.pack(fill="x", pady=(12, 0))
        ttk.Button(buttons, text="刷新音频设备", command=self.refresh_devices).pack(side="left")
        ttk.Button(buttons, text="保存并应用", command=self.save).pack(side="right")
        ttk.Button(buttons, text="重启 Bridge", command=self.restart).pack(side="right", padx=8)

        ttk.Label(
            outer,
            text="关闭此窗口后程序继续在系统托盘运行。虚拟麦克风需要预先安装 VB-CABLE。",
            foreground="#666666",
        ).pack(anchor="w", pady=(12, 0))

    def _load_settings(self) -> None:
        settings = self.config.voice_settings()
        self.feed.set(str(settings["feed_output_device"]))
        self.virtual.set(str(settings["virtual_input_device"]))
        self.restore_mode.set(str(settings["restore_mode"]))
        self.restore.set(str(settings["restore_input_device"]))
        self.hotkey.insert(0, str(settings["hotkey"]))
        self.trigger.set(str(settings["trigger_mode"]))
        self.transport.set(self.config.bridge_transport())

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
            "restore_mode": self.restore_mode.get(),
            "restore_input_device": self.restore.get().strip(),
            "hotkey": self.hotkey.get().strip(),
            "trigger_mode": self.trigger.get(),
        }
        try:
            voice = self.runtime.command({"name": "set_voice_settings", "value": values})
            if not voice.get("ok"):
                raise RuntimeError(str(voice.get("error") or "语音设置无效"))
            selected = self.transport.get()
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

    def restart(self) -> None:
        try:
            self.runtime.restart()
        except Exception as exc:
            messagebox.showerror("PanPal", f"重启失败：{exc}")

    def toggle_hooks(self) -> None:
        installed = self.hooks_button.cget("text").startswith("移除")
        action = "uninstall_hooks" if installed else "install_hooks"
        try:
            result = self.runtime.command({"name": action})
        except Exception as exc:
            messagebox.showerror("PanPal", f"Hooks 更新失败：{exc}")
            return
        if not result.get("ok"):
            messagebox.showerror("PanPal", f"Hooks 更新失败：{result.get('error')}")
            return
        self.hooks_button.configure(
            text="移除会话 Hooks" if result.get("hooks_installed") else "安装会话 Hooks"
        )
        installed = bool(result.get("hooks_installed"))
        listening = bool(result.get("hooks_listening"))
        command = str(result.get("hooks_command") or "")
        last_event = result.get("hooks_last_event_ms")
        if installed:
            message = (
                "Hooks 配置写入并回读成功。\n"
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
            message = "Hooks 已移除，并已回读确认。"
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
        self.root.after(1000, self._poll_status)

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
            detail = error
        self._set_color(color, label, detail)
        self.hooks_button.configure(
            text="移除会话 Hooks" if codex.get("hooks_installed") else "安装会话 Hooks"
        )
        if not codex.get("hooks_installed"):
            hooks_status = "Hooks 状态：未安装"
        elif not codex.get("hooks_listening"):
            hooks_status = "Hooks 状态：配置已写入，但本地接收端未运行"
        elif codex.get("hooks_last_event_ms"):
            hooks_status = f"Hooks 状态：已验证，最近收到 {codex.get('hooks_last_event')} 事件"
        else:
            hooks_status = "Hooks 状态：已安装、接收端运行中，尚未收到事件"
        self.hooks_status_label.configure(text=hooks_status)

        code = str(pairing.get("code") or "") if pairing else ""
        self.pair_label.configure(text=f"配对码  {code}" if code else "")
        if code and code != self.last_pairing_code:
            self.last_pairing_code = code
            self.show()
            self.root.bell()

    def _set_color(self, color: str, label: str, detail: str) -> None:
        palette = {"green": "#27ae60", "yellow": "#f2b01e", "red": "#d64541"}
        self.status_dot.delete("all")
        self.status_dot.create_oval(2, 2, 16, 16, fill=palette[color], outline="")
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

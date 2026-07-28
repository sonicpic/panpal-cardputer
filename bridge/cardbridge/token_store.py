from __future__ import annotations

import base64
import ctypes
import hashlib
import os
import sys
from pathlib import Path
from typing import Protocol


class TokenStore(Protocol):
    def get(self, device_id: str) -> str | None: ...

    def put(self, device_id: str, token: str) -> None: ...

    def delete(self, device_id: str) -> None: ...


class WindowsDpapiTokenStore:
    """Persist pairing tokens protected by the current Windows user profile."""

    _CRYPTPROTECT_UI_FORBIDDEN = 0x1

    class _DataBlob(ctypes.Structure):
        _fields_ = [("cbData", ctypes.c_uint32), ("pbData", ctypes.POINTER(ctypes.c_byte))]

    def __init__(
        self,
        directory: Path | None = None,
        *,
        purpose: str = "PanPal pairing token",
        secret_label: str = "pairing token",
    ) -> None:
        if sys.platform != "win32":
            raise RuntimeError("Windows DPAPI is only available on Windows")
        app_data = os.environ.get("APPDATA")
        root = (Path(app_data) / "CodexDeck" if app_data
                else Path.home() / "AppData" / "Roaming" / "CodexDeck")
        self.directory = directory or root / "pairing-secrets"
        self.purpose = purpose
        self.secret_label = secret_label
        self.directory.mkdir(parents=True, exist_ok=True)
        self._crypt32 = ctypes.WinDLL("Crypt32", use_last_error=True)
        self._kernel32 = ctypes.WinDLL("Kernel32", use_last_error=True)
        self._crypt32.CryptProtectData.argtypes = [
            ctypes.POINTER(self._DataBlob), ctypes.c_wchar_p,
            ctypes.POINTER(self._DataBlob), ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_uint32, ctypes.POINTER(self._DataBlob),
        ]
        self._crypt32.CryptProtectData.restype = ctypes.c_bool
        self._crypt32.CryptUnprotectData.argtypes = [
            ctypes.POINTER(self._DataBlob), ctypes.POINTER(ctypes.c_wchar_p),
            ctypes.POINTER(self._DataBlob), ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_uint32, ctypes.POINTER(self._DataBlob),
        ]
        self._crypt32.CryptUnprotectData.restype = ctypes.c_bool
        self._kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        self._kernel32.LocalFree.restype = ctypes.c_void_p

    def _path(self, device_id: str) -> Path:
        digest = hashlib.sha256(device_id.encode("utf-8")).hexdigest()
        return self.directory / f"{digest}.dpapi"

    @staticmethod
    def _blob(payload: bytes) -> tuple["WindowsDpapiTokenStore._DataBlob", object]:
        buffer = ctypes.create_string_buffer(payload)
        return (
            WindowsDpapiTokenStore._DataBlob(
                len(payload), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_byte))
            ),
            buffer,
        )

    def _protect(self, value: bytes) -> bytes:
        source, _source_buffer = self._blob(value)
        target = self._DataBlob()
        if not self._crypt32.CryptProtectData(
            ctypes.byref(source), self.purpose, None, None, None,
            self._CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(target),
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return ctypes.string_at(target.pbData, target.cbData)
        finally:
            self._kernel32.LocalFree(target.pbData)

    def _unprotect(self, value: bytes) -> bytes:
        source, _source_buffer = self._blob(value)
        target = self._DataBlob()
        description = ctypes.c_wchar_p()
        if not self._crypt32.CryptUnprotectData(
            ctypes.byref(source), ctypes.byref(description), None, None, None,
            self._CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(target),
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return ctypes.string_at(target.pbData, target.cbData)
        finally:
            if description:
                self._kernel32.LocalFree(description)
            self._kernel32.LocalFree(target.pbData)

    def get(self, device_id: str) -> str | None:
        path = self._path(device_id)
        if not path.exists():
            return None
        try:
            encrypted = base64.b64decode(path.read_bytes(), validate=True)
            return self._unprotect(encrypted).decode("ascii")
        except (OSError, ValueError, UnicodeDecodeError) as exc:
            raise RuntimeError(f"cannot read {self.secret_label} for {device_id}") from exc

    def put(self, device_id: str, token: str) -> None:
        encrypted = self._protect(token.encode("ascii"))
        target = self._path(device_id)
        temporary = target.with_suffix(".tmp")
        temporary.write_bytes(base64.b64encode(encrypted))
        os.replace(temporary, target)

    def delete(self, device_id: str) -> None:
        self._path(device_id).unlink(missing_ok=True)

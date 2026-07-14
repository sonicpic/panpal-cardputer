from __future__ import annotations

import argparse
import asyncio
import logging
import signal
from pathlib import Path

from .server import BridgeApp


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Cardputer ADV Mac bridge")
    result.add_argument("--host", default="0.0.0.0")
    result.add_argument("--port", type=int, default=7788, help="TCP control port")
    result.add_argument("--udp-port", type=int, default=7789)
    result.add_argument("--audio-device", default="BlackHole 2ch")
    result.add_argument("--jitter-ms", type=int, default=100)
    result.add_argument("--config", type=Path)
    result.add_argument("--no-audio", action="store_true", help="validate/drop audio without sound output")
    result.add_argument("--dry-run", action="store_true", help="log key events instead of injecting them")
    result.add_argument("--no-mdns", action="store_true", help="disable service discovery advertisement")
    result.add_argument("--record", type=Path, help="also write received PCM to this WAV file (diagnostic)")
    result.add_argument("-v", "--verbose", action="store_true")
    return result


async def run(args: argparse.Namespace) -> None:
    app = BridgeApp(
        host=args.host,
        tcp_port=args.port,
        udp_port=args.udp_port,
        config_path=args.config,
        audio_device=args.audio_device,
        jitter_ms=args.jitter_ms,
        no_audio=args.no_audio,
        dry_run=args.dry_run,
        advertise=not args.no_mdns,
        record_path=args.record,
    )
    await app.start()
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(signum, stop.set)
        except NotImplementedError:
            pass
    try:
        await stop.wait()
    finally:
        await app.stop()


def main() -> None:
    args = parser().parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    try:
        asyncio.run(run(args))
    except RuntimeError as exc:
        raise SystemExit(f"CardBridge startup failed: {exc}") from exc


if __name__ == "__main__":
    main()

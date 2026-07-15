#!/usr/bin/env python3
"""Build the embedded CardBridge UI font from a pinned open-source font."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import tempfile
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_URL = (
    "https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.005R/"
    "SubsetOTF/CN/SourceHanSansCN-Medium.otf"
)
FONT_SHA256 = "a94e558a2fe972bee4f46bce0843abff37063fd68c33f1e7d9058f6f09432b01"
CONVERTER_VERSION = "1.5.3"
PIXEL_SIZE = 15
BITS_PER_PIXEL = 4
OUTPUT_SHA256 = "13bd9ee3a924735e47f0e8a4995acc3dbc4aa955b6c247d13a9b901a67113585"


def glyph_codepoints() -> list[int]:
    """Return printable ASCII plus every character represented by GB2312."""

    points = set(range(0x20, 0x7F))
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                text = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            points.update(ord(character) for character in text if ord(character) <= 0xFFFF)
    points.update(map(ord, "·—–…“”‘’《》【】（）！？，。：；"))
    return sorted(points)


def range_argument(codepoints: list[int]) -> str:
    ranges: list[tuple[int, int]] = []
    start = previous = codepoints[0]
    for value in codepoints[1:]:
        if value == previous + 1:
            previous = value
            continue
        ranges.append((start, previous))
        start = previous = value
    ranges.append((start, previous))
    return ",".join(
        f"0x{start:X}" if start == end else f"0x{start:X}-0x{end:X}"
        for start, end in ranges
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def obtain_font(requested: Path | None) -> Path:
    destination = requested or (
        Path(tempfile.gettempdir()) / "cardbridge-fonts" / "SourceHanSansCN-Medium-2.005R.otf"
    )
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix(".download")
        request = urllib.request.Request(FONT_URL, headers={"User-Agent": "CardBridge-font-builder"})
        with urllib.request.urlopen(request) as response, temporary.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        temporary.replace(destination)
    actual = sha256(destination)
    if actual != FONT_SHA256:
        raise SystemExit(
            f"Unexpected Source Han Sans checksum for {destination}: {actual}"
        )
    return destination


def emit_binary(binary: Path, output: Path, glyph_count: int) -> None:
    data = binary.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != OUTPUT_SHA256:
        raise SystemExit(f"Unexpected generated font checksum: {actual}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    print(
        f"Wrote {output}: {len(data):,} bytes, {glyph_count:,} glyphs, "
        f"sha256={actual}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, help="pinned SourceHanSansCN-Medium OTF")
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "assets" / "fonts" / "cardbridge-ui-15.bff",
    )
    args = parser.parse_args()

    font = obtain_font(args.font)
    codepoints = glyph_codepoints()
    with tempfile.TemporaryDirectory(prefix="cardbridge-ui-font-") as directory:
        binary = Path(directory) / "cardbridge-ui-font.bff"
        subprocess.run(
            [
                "npx",
                "--yes",
                f"lv_font_conv@{CONVERTER_VERSION}",
                "--font",
                str(font),
                "--range",
                range_argument(codepoints),
                "--size",
                str(PIXEL_SIZE),
                "--format",
                "bin",
                "--bpp",
                str(BITS_PER_PIXEL),
                "--no-kerning",
                "-o",
                str(binary),
            ],
            check=True,
        )
        emit_binary(binary, args.output, len(codepoints))


if __name__ == "__main__":
    main()

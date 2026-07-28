from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "windows" / "assets"


def build(name: str, color: str) -> None:
    image = Image.new("RGBA", (64, 64), (18, 22, 30, 255))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((4, 4, 59, 59), radius=13, outline=(220, 230, 240), width=3)
    draw.ellipse((18, 18, 46, 46), fill=color)
    draw.rectangle((28, 8, 36, 18), fill=(220, 230, 240))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    image.save(
        OUTPUT / f"codex-deck-{name}.ico",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)],
    )


if __name__ == "__main__":
    build("green", "#27ae60")
    build("yellow", "#f2b01e")
    build("red", "#d64541")

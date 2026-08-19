#!/usr/bin/env python3
"""Generate the checked-in ZMouseShow A01 icon resources.

The script intentionally depends only on Pillow so the multi-resolution ICO
files can be reproduced without requiring an SVG renderer.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "resources" / "branding"

NAVY = "#102544"
CYAN = "#7DE7FF"
BLUE = "#3C89FF"
WHITE = "#F1FBFF"

APP_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
TRAY_SIZES = (16, 20, 24, 32, 48)
SUPERSAMPLE = 4

APP_Z_POINTS = (
    (5.55, 4.85),
    (15.85, 4.85),
    (14.45, 6.75),
    (9.75, 6.75),
    (6.75, 13.25),
    (15.55, 13.25),
    (14.15, 15.15),
    (4.15, 15.15),
    (7.15, 8.65),
    (11.55, 8.65),
    (12.95, 6.75),
    (4.75, 6.75),
)

TRAY_Z_POINTS = (
    (5.1, 4.8),
    (19.2, 4.8),
    (17.2, 7.6),
    (11.7, 7.6),
    (8.4, 16.4),
    (18.9, 16.4),
    (16.9, 19.2),
    (4.6, 19.2),
    (7.9, 10.4),
    (13.3, 10.4),
    (15.3, 7.6),
    (4.2, 7.6),
)


def rgb(hex_color: str) -> tuple[int, int, int]:
    value = hex_color.removeprefix("#")
    return tuple(int(value[index : index + 2], 16) for index in (0, 2, 4))


def scaled_points(points: tuple[tuple[float, float], ...], factor: float) -> list[tuple[float, float]]:
    return [(x * factor, y * factor) for x, y in points]


def diagonal_gradient(size: int, start: str, end: str) -> Image.Image:
    start_rgb = rgb(start)
    end_rgb = rgb(end)
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    denominator = max(1, (size - 1) * 2)
    for y in range(size):
        for x in range(size):
            amount = (x + y) / denominator
            pixels[x, y] = tuple(
                round(start_channel + (end_channel - start_channel) * amount)
                for start_channel, end_channel in zip(start_rgb, end_rgb)
            ) + (255,)
    return image


def render_app_icon(size: int) -> Image.Image:
    canvas_size = size * SUPERSAMPLE
    unit = canvas_size / 64.0
    image = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(
        (3.2 * unit, 3.2 * unit, 60.8 * unit, 60.8 * unit),
        radius=14.0 * unit,
        fill=rgb(NAVY) + (255,),
    )

    z_mask = Image.new("L", image.size, 0)
    mask_draw = ImageDraw.Draw(z_mask)
    mask_draw.polygon(scaled_points(APP_Z_POINTS, 3.2 * unit), fill=255)
    image.alpha_composite(Image.composite(diagonal_gradient(canvas_size, CYAN, BLUE), Image.new("RGBA", image.size), z_mask))

    draw = ImageDraw.Draw(image)
    foreground = rgb(WHITE) + (255,)
    line_width = max(1, round(1.8 * unit))
    detail = size > 24
    radius = (7.2 if detail else 5.3) * unit
    center = (32.0 * unit, 33.0 * unit)
    draw.ellipse(
        (center[0] - radius, center[1] - radius, center[0] + radius, center[1] + radius),
        outline=foreground,
        width=line_width,
    )
    dot_radius = 2.3 * unit
    draw.ellipse(
        (
            center[0] - dot_radius,
            center[1] - dot_radius,
            center[0] + dot_radius,
            center[1] + dot_radius,
        ),
        fill=foreground,
    )
    if detail:
        for start, end in (
            ((32, 21.5), (32, 25.5)),
            ((32, 40.5), (32, 44.5)),
            ((20.5, 33), (24.5, 33)),
            ((39.5, 33), (43.5, 33)),
        ):
            draw.line(
                (start[0] * unit, start[1] * unit, end[0] * unit, end[1] * unit),
                fill=foreground,
                width=line_width,
            )

    return image.resize((size, size), Image.Resampling.LANCZOS)


def render_tray_icon(size: int, foreground: str) -> Image.Image:
    canvas_size = size * SUPERSAMPLE
    unit = canvas_size / 24.0
    image = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    color = rgb(foreground) + (255,)
    draw.polygon(scaled_points(TRAY_Z_POINTS, unit), fill=color)

    center = (12.0 * unit, 12.4 * unit)
    hole_radius = 2.25 * unit
    draw.ellipse(
        (
            center[0] - hole_radius,
            center[1] - hole_radius,
            center[0] + hole_radius,
            center[1] + hole_radius,
        ),
        fill=(0, 0, 0, 0),
    )
    dot_radius = 1.0 * unit
    draw.ellipse(
        (
            center[0] - dot_radius,
            center[1] - dot_radius,
            center[0] + dot_radius,
            center[1] + dot_radius,
        ),
        fill=color,
    )

    if size > 20:
        line_width = max(1, round(1.7 * unit))
        for start, end in (
            ((12, 1.8), (12, 4.1)),
            ((12, 20.0), (12, 22.2)),
            ((1.8, 12), (4.1, 12)),
            ((20.0, 12), (22.2, 12)),
        ):
            draw.line(
                (start[0] * unit, start[1] * unit, end[0] * unit, end[1] * unit),
                fill=color,
                width=line_width,
            )

    return image.resize((size, size), Image.Resampling.LANCZOS)


def save_ico(path: Path, frames: list[Image.Image]) -> None:
    largest = frames[-1]
    largest.save(
        path,
        format="ICO",
        sizes=[frame.size for frame in frames],
        append_images=frames[:-1],
    )


def app_svg(*, small: bool) -> str:
    detail = "" if small else """
  <path d="M32 21.5v4M32 40.5v4M20.5 33h4M39.5 33h4" fill="none" stroke="#F1FBFF" stroke-width="1.8" stroke-linecap="round"/>"""
    radius = "5.3" if small else "7.2"
    label = "small application" if small else "application"
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 64 64" role="img" aria-labelledby="title desc">
  <title id="title">ZMouseShow {label} icon</title>
  <desc id="desc">A cyan-blue ribbon Z with a locator focus on a dark navy rounded square.</desc>
  <defs>
    <linearGradient id="ribbon" x1="14" y1="14" x2="49" y2="49" gradientUnits="userSpaceOnUse">
      <stop stop-color="{CYAN}"/>
      <stop offset="1" stop-color="{BLUE}"/>
    </linearGradient>
  </defs>
  <rect x="3.2" y="3.2" width="57.6" height="57.6" rx="14" fill="{NAVY}"/>
  <g transform="scale(3.2)">
    <path d="M5.55 4.85H15.85L14.45 6.75H9.75L6.75 13.25H15.55L14.15 15.15H4.15L7.15 8.65H11.55L12.95 6.75H4.75Z" fill="url(#ribbon)"/>
  </g>
  <circle cx="32" cy="33" r="{radius}" fill="none" stroke="#F1FBFF" stroke-width="1.8"/>
  <circle cx="32" cy="33" r="2.3" fill="#F1FBFF"/>{detail}
</svg>
"""


def tray_svg(foreground: str, label: str) -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" role="img" aria-labelledby="title desc">
  <title id="title">ZMouseShow {label} notification area icon</title>
  <desc id="desc">A monochrome ribbon Z with a locator focus for the Windows notification area.</desc>
  <defs>
    <mask id="focus-hole">
      <rect width="24" height="24" fill="white"/>
      <circle cx="12" cy="12.4" r="2.25" fill="black"/>
    </mask>
  </defs>
  <g fill="{foreground}">
    <path d="M5.1 4.8h14.1l-2 2.8h-5.5l-3.3 8.8h10.5l-2 2.8H4.6l3.3-8.8h5.4l2-2.8H4.2Z" mask="url(#focus-hole)"/>
    <circle cx="12" cy="12.4" r="1"/>
  </g>
  <path d="M12 1.8v2.3M12 20v2.2M1.8 12h2.3M20 12h2.2" stroke="{foreground}" stroke-width="1.7" stroke-linecap="round"/>
</svg>
"""


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)

    app_frames = [render_app_icon(size) for size in APP_SIZES]
    tray_light_frames = [render_tray_icon(size, WHITE) for size in TRAY_SIZES]
    tray_dark_frames = [render_tray_icon(size, NAVY) for size in TRAY_SIZES]

    save_ico(OUTPUT / "zmouse-app.ico", app_frames)
    save_ico(OUTPUT / "zmouse-tray-light.ico", tray_light_frames)
    save_ico(OUTPUT / "zmouse-tray-dark.ico", tray_dark_frames)
    app_frames[-1].save(OUTPUT / "zmouse-app-icon.png", format="PNG")

    (OUTPUT / "zmouse-app-icon.svg").write_text(app_svg(small=False), encoding="utf-8", newline="\n")
    (OUTPUT / "zmouse-app-icon-small.svg").write_text(app_svg(small=True), encoding="utf-8", newline="\n")
    (OUTPUT / "zmouse-tray-light.svg").write_text(tray_svg(WHITE, "light"), encoding="utf-8", newline="\n")
    (OUTPUT / "zmouse-tray-dark.svg").write_text(tray_svg(NAVY, "dark"), encoding="utf-8", newline="\n")

    print(f"Generated A01 icon assets in {OUTPUT}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Draws the Soundgineer icon, app/icons/soundgineer.svg: a wavetable, frames
of a waveform stacked in perspective, morphing from a sine at the back to a
sawtooth at the front. Colors are the app's default theme (Tokyo Night).

    python3 tools/make-icon.py
"""
import math
from pathlib import Path

SIZE = 256
BG, BLUE, PINK, MUTED = "#1a1b26", "#7aa2f7", "#f7768e", "#3b4261"
FRAMES = 6


def wave(x, morph):
    """One cycle at x in [0, 1): a sine blended into a soft sawtooth."""
    sine = math.sin(2 * math.pi * x)
    # Band-limited saw from its first harmonics, so the corner stays round.
    # The Lanczos factor smooths away the ripple a plain sum would have.
    n = 12
    saw = sum((-1) ** (k + 1) * math.sin(2 * math.pi * k * x) / k
              * (math.sin(math.pi * k / n) / (math.pi * k / n)) for k in range(1, n)) * (2 / math.pi)
    return (1 - morph) * sine + morph * saw * 1.25


def frame_path(i):
    morph = i / (FRAMES - 1)
    # Back frames sit up and to the right; the front one is lowest and widest.
    back = FRAMES - 1 - i
    # The stack spans 130 + 5 * 8 = 170 wide; centered on the icon.
    x0, width = 43 + back * 8, 130
    y0, amp = 158 - back * 12, 34
    pts = []
    for s in range(121):
        x = s / 120
        pts.append((x0 + x * width, y0 - amp * wave(x, morph)))
    return "M" + " L".join(f"{px:.1f} {py:.1f}" for px, py in pts)


def lerp(a, b, t):
    ca = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    cb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02x}" for x, y in zip(ca, cb))


parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}" width="{SIZE}" height="{SIZE}">',
    "  <title>Soundgineer</title>",
    f'  <rect x="8" y="8" width="240" height="240" rx="52" fill="{BG}"/>',
    f'  <rect x="9.5" y="9.5" width="237" height="237" rx="50.5" fill="none" stroke="{MUTED}" stroke-width="3"/>',
    '  <g fill="none" stroke-linecap="round" stroke-linejoin="round">',
]
for i in range(FRAMES):
    t = i / (FRAMES - 1)
    front = i == FRAMES - 1
    color = PINK if front else lerp(MUTED, BLUE, 0.35 + 0.65 * t)
    width = 9 if front else 5
    opacity = 1 if front else 0.45 + 0.5 * t
    parts.append(f'    <path d="{frame_path(i)}" stroke="{color}" stroke-width="{width}" opacity="{opacity:.2f}"/>')
parts += ["  </g>", "</svg>", ""]

out = Path(__file__).resolve().parent.parent / "app" / "icons" / "soundgineer.svg"
out.write_text("\n".join(parts))
print(f"wrote {out}")

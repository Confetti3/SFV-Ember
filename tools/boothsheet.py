"""Contact sheets of the captured cutouts, to check a run by eye.

  python -I tools/boothsheet.py [--store D:\\sfv-ember-renders] [--fighters RYU,KEN] [--source masters|pack]

Writes <store>\\contact\\<CODE>.png (every costume x colour of one fighter, a row per costume,
each cell labelled "CODE costume/colour"; a look with no cutout yet is a crossed-out cell) and
<store>\\contact\\overview.png (the first colour of every costume, grouped by fighter). The
background is the shell's charcoal. Needs Pillow and numpy (tools/booth-requirements.txt).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from PIL import Image, ImageDraw, ImageFont
    import boothpack as bp
except ImportError as e:  # pragma: no cover
    sys.exit(f"boothsheet needs Pillow and numpy ({e}): python -m pip install -r tools/booth-requirements.txt")

CHARCOAL = (30, 30, 30)
CELL_LINE = (52, 52, 52)
LABEL = (200, 200, 200)
MISSING = (150, 60, 60)


def _font(size: int):
    try:
        return ImageFont.load_default(size=size)  # Pillow 10.1+
    except TypeError:  # pragma: no cover - an older Pillow
        return ImageFont.load_default()


def cutout_source(store: Path, source: str, code: str, costume: int, colour: int) -> Path:
    if source == "pack":
        return store / "pack" / bp.cutout_relpath(code, costume, colour)
    return bp.master_path(store, code, costume, colour)


def draw_cell(sheet: Image.Image, draw: ImageDraw.ImageDraw, font, path: Path, origin: tuple[int, int],
              cell: tuple[int, int], label: str, label_h: int) -> bool:
    x, y = origin
    width, height = cell
    draw.rectangle([x, y, x + width - 1, y + height + label_h - 1], outline=CELL_LINE)
    found = path.is_file()
    if found:
        try:
            with Image.open(path) as im:
                im = im.convert("RGBA")
                scale = min(width / im.width, height / im.height)
                size = (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
                im = im.convert("RGBa").resize(size, Image.Resampling.LANCZOS).convert("RGBA")
            sheet.alpha_composite(im, (x + (width - size[0]) // 2, y + height - size[1]))
        except OSError:
            found = False
    if not found:
        draw.line([x + 4, y + 4, x + width - 5, y + height - 5], fill=MISSING)
        draw.line([x + width - 5, y + 4, x + 4, y + height - 5], fill=MISSING)
    draw.text((x + 3, y + height + 1), label, fill=LABEL if found else MISSING, font=font)
    return found


def sheet_for_fighter(store: Path, space: dict, code: str, source: str, cell: tuple[int, int] = (128, 192)) -> tuple[Image.Image, int, int]:
    fighter = space["fighters"][code]
    label_h = 16
    columns = max(len(c) for c in fighter["costumes"].values())
    rows = len(fighter["costumes"])
    sheet = Image.new("RGBA", (columns * cell[0], rows * (cell[1] + label_h)), CHARCOAL + (255,))
    draw, font = ImageDraw.Draw(sheet), _font(12)
    have = total = 0
    for r, (costume, colours) in enumerate(fighter["costumes"].items()):
        for c, colour in enumerate(colours):
            total += 1
            have += draw_cell(sheet, draw, font, cutout_source(store, source, code, costume, colour),
                              (c * cell[0], r * (cell[1] + label_h)), cell, f"{code} {costume}/{colour}", label_h)
    return sheet, have, total


def overview(store: Path, space: dict, codes: list[str], source: str, cell: tuple[int, int] = (96, 144), columns: int = 32) -> tuple[Image.Image, int, int]:
    label_h = 14
    items = [(code, costume, colours[0]) for code in codes for costume, colours in space["fighters"][code]["costumes"].items()]
    rows = max(1, -(-len(items) // columns))
    sheet = Image.new("RGBA", (columns * cell[0], rows * (cell[1] + label_h)), CHARCOAL + (255,))
    draw, font = ImageDraw.Draw(sheet), _font(11)
    have = 0
    for i, (code, costume, colour) in enumerate(items):
        have += draw_cell(sheet, draw, font, cutout_source(store, source, code, costume, colour),
                          ((i % columns) * cell[0], (i // columns) * (cell[1] + label_h)), cell,
                          f"{code} {costume}/{colour}", label_h)
    return sheet, have, len(items)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--store", default=bp.DEFAULT_STORE)
    p.add_argument("--pick-space", default=str(bp.DEFAULT_PICK_SPACE))
    p.add_argument("--fighters", help="comma-separated codes (default: every fighter that has a cutout)")
    p.add_argument("--source", choices=["masters", "pack"], default="masters",
                   help="the 512x768 masters (default) or the shipped 256x384 palette cutouts")
    p.add_argument("--no-overview", action="store_true")
    a = p.parse_args(argv)
    space = bp.load_space(a.pick_space)
    store = Path(a.store)
    codes = [c.strip().upper() for c in a.fighters.split(",")] if a.fighters else list(space["fighters"])
    unknown = [c for c in codes if c not in space["fighters"]]
    if unknown:
        p.error(f"not in the pick space: {', '.join(unknown)}")
    out = store / "contact"
    out.mkdir(parents=True, exist_ok=True)
    shown = []
    for code in codes:
        sheet, have, total = sheet_for_fighter(store, space, code, a.source)
        if have == 0 and not a.fighters:
            continue
        sheet.convert("RGB").save(out / f"{code}.png", optimize=False)
        shown.append(code)
        print(f"{code}: {have}/{total} looks -> {out / (code + '.png')}")
    if not a.no_overview and shown:
        sheet, have, total = overview(store, space, shown, a.source)
        sheet.convert("RGB").save(out / "overview.png", optimize=False)
        print(f"overview: {have}/{total} first colours -> {out / 'overview.png'}")
    if not shown:
        print("no cutouts found")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

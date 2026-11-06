# SFV Ember brand artwork

SFV Ember's mark is Ember's own: the fire character 火 inside its open brush circle, with the tail (the Ember library's `assets/brand/source/ember-mark.svg`, original artwork drawn for Ember). Its shapes are untouched; only its colours are SFV Ember's Band theme's (docs/design/EMBER_LIBRARY.md section 16). Ember's ivory `#F3EBDD` becomes the theme's text `#EDECE0`, its orange `#FF8738` (the left dot and the tail) the theme's gold `#DDB961`, and the tail's light orange `#F3C58B` the gold's light step `#F2DD9D`. SFV Ember draws no picture behind its menus: its ground is flat ink.

| File | What it is |
|---|---|
| `sfv-ember-mark.svg` | The mark's source: Ember's SVG with its three fills swapped |
| `sfv-ember-mark.png` | Mark export, 1024 x 1024 RGBA |
| `sfv-ember-mark.rgba` | 128 x 128 straight RGBA, embedded in the font atlas at build time (CMakeLists.txt) |
| `../../assets/brand/sfv-ember-menu-background.png` | The menu's ground, 2048 x 1152, flat `#100F0D` (the theme's ink) |

## Rebuilding

`scripts/build-sfv-brand.py --ember-root <Ember library>` (Pillow and numpy) writes all four. It swaps the SVG's fills, and recolours Ember's own export (`src/ui/ember.png`) pixel by pixel: each pixel keeps its alpha and moves by its weights over Ember's three fills, so edges and texture stay as they are. Normal builds use the checked-in exports and need neither Python nor Pillow.

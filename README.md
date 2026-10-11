# Chili City

![Build](https://github.com/chili-chip/chili-city/actions/workflows/build.yml/badge.svg)

![Chili City splash](assets/splash.png)

A tiny SimCity-like city builder for the [VGC Zero](https://chilichip.eu) handheld, written in C++ with the chili-chip fork of the [32blit SDK](https://github.com/chili-chip/32blit-sdk).

Lay roads, zone land for homes, shops and factories, and watch the city grow month by month while you keep the budget out of the red.

## How to play

- The map is 32×32 tiles; the screen shows 16×16 and scrolls with the cursor.
- Zones only grow when they touch a road.
  - **Residential** grows when there are jobs (the first few homes fill up anyway).
  - **Commercial** grows when there are enough residents to shop.
  - **Industrial** grows when there are enough residents to work.
- You start with **$5000**. Building costs money:

  | Tool | Cost |
  |---|---|
  | Road | $10 |
  | Residential / Commercial / Industrial zone | $50 |
  | Bulldoze | $5 |

- Every month you collect taxes from residents and businesses and pay $1 upkeep per road tile.
- You can only build on grass. Bulldoze first to replace something.

## Controls

| Action | VGC Zero | PC (SDL build) |
|---|---|---|
| Move cursor | D-pad | Arrow keys or WASD |
| Use tool | A | Z |
| Next tool | B | X |
| Stats screen | Y | V |

## Building

Clone this repo next to the [32blit-sdk](https://github.com/chili-chip/32blit-sdk) (and, for the console, [pico-sdk](https://github.com/chili-chip/pico-sdk) and [pico-extras](https://github.com/chili-chip/pico-extras)):

```
chilichip/
├── 32blit-sdk/
├── pico-sdk/
├── pico-extras/
└── chili-city/
```

`CMakePresets.json` needs CMake 3.28 or newer. Setup steps for the tools are in the [Linux quickstart](https://chilichip.eu/docs/game-sdk/quickstart/linux-quickstart/).

**PC (Linux SDL):**

```
cmake --preset linux
cmake --build out/build/linux
./out/build/linux/chili-city.elf
```

**VGC Zero:**

```
cmake --preset vgc
cmake --build out/build/vgc
```

Then hold **BOOT** while plugging the console in, and copy `out/build/vgc/chili-city.uf2` to the `RP2350` drive. The console reboots into the game.

Ready-made `.uf2` files are attached to each [release](https://github.com/chili-chip/chili-city/releases).

## Art

All graphics are 8×8 sprites in `assets/tiles.png` (8 sprites per row):

| Sprites | Contents |
|---|---|
| 0 | grass |
| 16–31 | road, picked by which neighbours are roads (north 1, east 2, south 4, west 8) |
| 32–35 | residential, levels 0–3 |
| 40–43 | commercial, levels 0–3 |
| 48–51 | industrial, levels 0–3 |

Redraw them in any pixel editor; keep the layout and the game picks them up.

## License

MIT, see [LICENSE](LICENSE). Based on the [32blit boilerplate](https://github.com/32blit/32blit-boilerplate).

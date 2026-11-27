#!/usr/bin/env python3
"""
Extract the graphics from Sierra's Championship Boxing (1984, IBM PC CGA version)
into PNG files.

Usage:
    python3 extract_graphics.py [GAME_DIR [OUT_DIR]] [--scale N] [--palette 0|1] [--low]

Works on both known IBM releases (detected automatically):
  * the 1987 cracked booter files (BOXING.OVR, BOXING.PIC, BOXING.BB ...)
  * the file-based "SIERRACH" build (KO.EXE, *.PIX, *BXR0/1.SPT, GIRL.SPT ...)

With no GAME_DIR it extracts every release folder found under
../Original_IBM_CGA into ../Graphics/<folder>. Defaults: scale 2,
palette 1 (black / cyan / magenta / white, high intensity), which is what the
game itself selects.

Requires Pillow (pip install pillow). Original files are only ever read.

Formats (see docs/ANALYSIS.md for the full write-up):

  * Full screens (BOXING.PIC, .PI2, .PI3, .PI4, .PI5)
      A stream of (count, value) byte pairs. Pairs alternate between the
      even-scanline bank (B800:0000) and the odd-scanline bank (B800:2000),
      one pair each, until each bank holds 8000 bytes (80 bytes x 100 lines).
      A count of 0 means 1. Result = a raw 320x200 4-colour CGA screen.

  * Sprites. Every sprite starts with little-endian words
      width (in 16-bit words, 1 word = 8 pixels), height (in line PAIRS),
      screen offset (signed, relative positioning).
    Pixel data is stored one "row" at a time, each row covering an even and an
    odd scanline, word by word: [even word][odd word][even word][odd word]...
    So a sprite of w x h is (w*8) x (h*2) pixels.
    There are three flavours, distinguished by how the game draws them:
      opaque   : 6-byte header, w*h*4 bytes of data, then 2 padding bytes
      head     : 8-byte header (extra offset word), w*h*4 bytes of data
      masked   : 6-byte header; in every row the first and last word columns
                 are stored as (AND-mask, OR-data) pairs for each scanline, the
                 middle columns are opaque.  Row size = 4*w + 8 bytes.
"""
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("This script needs Pillow:  python3 -m pip install pillow")

# --- CGA palettes ---------------------------------------------------------
PALETTES = {
    (1, True):  [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)],
    (1, False): [(0, 0, 0), (0, 170, 170), (170, 0, 170), (170, 170, 170)],
    (0, True):  [(0, 0, 0), (85, 255, 85), (255, 85, 85), (255, 255, 85)],
    (0, False): [(0, 0, 0), (0, 170, 0), (170, 0, 0), (170, 85, 0)],
}
TRANSPARENT = (0, 0, 0, 0)


def pixels_of(byte):
    """Four 2-bit pixels in a CGA byte, leftmost first."""
    return [(byte >> s) & 3 for s in (6, 4, 2, 0)]


# --- Full screens ---------------------------------------------------------
def decode_screen(data):
    """Return 16 KB of CGA video memory, exactly as the game's loader fills it."""
    vram = bytearray(0x4000)
    even, odd, p = 0x0000, 0x2000, 0
    while (even < 0x1F40 or odd < 0x3F40) and p + 1 < len(data):
        if even < 0x1F40:
            n, v = data[p] or 1, data[p + 1]
            p += 2
            vram[even:even + n] = bytes([v]) * n
            even += n
        if odd < 0x3F40 and p + 1 < len(data):
            n, v = data[p] or 1, data[p + 1]
            p += 2
            vram[odd:odd + n] = bytes([v]) * n
            odd += n
    return vram


def vram_to_image(vram, pal):
    im = Image.new("RGB", (320, 200))
    px = im.load()
    for y in range(200):
        row = (y >> 1) * 80 + (y & 1) * 0x2000
        for xb in range(80):
            for k, c in enumerate(pixels_of(vram[row + xb])):
                px[xb * 4 + k, y] = pal[c]
    return im


# --- Sprites --------------------------------------------------------------
def draw_opaque(data, p, w, h, pal):
    im = Image.new("RGBA", (w * 8, h * 2))
    px = im.load()
    for r in range(h):
        for c in range(w):
            for line in (0, 1):
                for half in (0, 1):
                    for k, col in enumerate(pixels_of(data[p])):
                        px[c * 8 + half * 4 + k, r * 2 + line] = pal[col] + (255,)
                    p += 1
    return im, p


def draw_masked(data, p, w, h, pal):
    im = Image.new("RGBA", (w * 8, h * 2))
    px = im.load()
    for r in range(h):
        for c in range(w):
            edge = c in (0, w - 1)
            for line in (0, 1):
                if edge:
                    mask, val = data[p:p + 2], data[p + 2:p + 4]
                    p += 4
                else:
                    mask, val = b"\0\0", data[p:p + 2]
                    p += 2
                for half in (0, 1):
                    m, v = pixels_of(mask[half]), pixels_of(val[half])
                    for k in range(4):
                        # mask bits 11 = keep what is already on screen
                        px[c * 8 + half * 4 + k, r * 2 + line] = (
                            TRANSPARENT if m[k] == 3 else pal[v[k]] + (255,))
    return im, p


def header(data, o):
    w, h, off = struct.unpack_from("<HHh", data, o)
    return w, h, off


def valid(w, h):
    return 0 < w <= 40 and 0 < h <= 60


def parse_boxer(data, pal):
    """Boxer files: legs/whole-body frames (opaque), heads (6x20), arms (masked)."""
    out, o, phase = [], 0, "body"
    while o + 6 <= len(data):
        w, h, off = header(data, o)
        if not valid(w, h):
            break
        if phase == "body" and (w, h) == (6, 20):
            phase = "head"
        if phase == "head" and (w, h) != (6, 20):
            phase = "arms"
        try:
            if phase == "body":
                im, p = draw_opaque(data, o + 6, w, h, pal)
                p += 2
            elif phase == "head":
                im, p = draw_opaque(data, o + 8, w, h, pal)
            else:
                im, p = draw_masked(data, o + 6, w, h, pal)
        except IndexError:
            break
        out.append((phase, o, w, h, off, im))
        o = p
    return out, o


def parse_opaque_list(data, pal, kind):
    out, o = [], 0
    while o + 6 <= len(data):
        w, h, off = header(data, o)
        if not valid(w, h) or o + 6 + 4 * w * h > len(data):
            break
        im, p = draw_opaque(data, o + 6, w, h, pal)
        out.append((kind, o, w, h, off, im))
        o = p + 2
    return out, o


def contact_sheet(frames, scale):
    gap = 4
    W = sum(f[-1].size[0] + gap for f in frames) + gap
    H = max(f[-1].size[1] for f in frames) + 2 * gap
    sheet = Image.new("RGBA", (W, H), (64, 64, 64, 255))
    x = gap
    for f in frames:
        sheet.alpha_composite(f[-1], (x, gap))
        x += f[-1].size[0] + gap
    return sheet.resize((W * scale, H * scale), Image.NEAREST)


# --- Release layouts -------------------------------------------------------
# Two IBM releases are known. They contain the same graphics data:
#   * "1987_Crack": the self-booting disk split into BOXING.* sector-slice files
#     (Bytten's loader). BOXING.WR is missing.
#   * "SierraCh": a file-based build (KO.EXE) with named files. Complete.
CHARACTERS = {"B": "black", "W": "white", "C": "cartoon", "K": "kangaroo"}
SIDES = {"B": "right_side", "R": "left_side"}  # per the decoded animation tables: xR = left boxer, xB = right boxer

LAYOUTS = {
    "1987_Crack": {
        "detect": "BOXING.OVR",
        "screens": [("BOXING.PIC", "title_main_menu"), ("BOXING.PI2", "gym"), ("BOXING.PI3", "fight_arena"),
                    ("BOXING.PI4", "end_of_round_statistics"), ("BOXING.PI5", "options")],
        "boxer": lambda ch, side: f"BOXING.{ch}{side}",
        "girl": ("BOXING.GRL", 0),
        "extras": ("BOXING.FG3", 0),
    },
    "SierraCh": {
        "detect": "KO.EXE",
        "screens": [("OSIDE.PIX", "title_main_menu"), ("GYM.PIX", "gym"), ("RING.PIX", "fight_arena"),
                    ("STAT.PIX", "end_of_round_statistics"), ("OPTION.PIX", "options")],
        # xBXR0.SPT = the xR (left-side) set, xBXR1.SPT = the xB (right-side) set
        "boxer": lambda ch, side: f"{ch}BXR{0 if side == 'R' else 1}.SPT",
        "girl": ("GIRL.SPT", 0),
        "extras": ("FIGHT.OLY", 22224),  # the FG3 sprite block lives inside the fight overlay
    },
}


def detect(game):
    for name, lay in LAYOUTS.items():
        if os.path.exists(os.path.join(game, lay["detect"])):
            return name
    return None


def extract(game, outdir, scale, pal):
    release = detect(game)
    if not release:
        print(f"skip {game}: no BOXING.OVR or KO.EXE found")
        return
    lay = LAYOUTS[release]
    print(f"== {release}: {game} -> {outdir}")

    def path(fn):
        return os.path.join(game, fn)

    def up(im):
        return im.resize((im.size[0] * scale, im.size[1] * scale), Image.NEAREST) if scale != 1 else im

    def save(im, *parts):
        fn = os.path.join(outdir, *parts)
        os.makedirs(os.path.dirname(fn), exist_ok=True)
        up(im).save(fn)

    for i, (fn, name) in enumerate(lay["screens"], 1):
        if os.path.exists(path(fn)):
            vram = decode_screen(open(path(fn), "rb").read())
            save(vram_to_image(vram, pal), "screens", f"{i}_{name}.png")
            print(f"screen  {fn} -> screens/{i}_{name}.png")

    for ch in "BWCK":
        for side in "RB":
            fn = lay["boxer"](ch, side)
            folder = f"{ch}{side}_{CHARACTERS[ch]}_{SIDES[side]}"
            if not os.path.exists(path(fn)):
                print(f"missing {fn} ({folder})")
                continue
            frames, end = parse_boxer(open(path(fn), "rb").read(), pal)
            n = {}
            for kind, o, w, h, off, im in frames:
                n[kind] = n.get(kind, 0) + 1
                save(im, "boxers", folder, f"{kind}_{n[kind]:02d}_at{o:04X}.png")
            contact_sheet(frames, scale).save(os.path.join(outdir, "boxers", f"{folder}_sheet.png"))
            print(f"boxer   {fn} -> {folder}: {len(frames)} frames {n}")

    for (fn, base), folder, prefix in ((lay["girl"], "ring_girl_and_logos", "GRL"),
                                       (lay["extras"], "fight_extras", "FG3")):
        if os.path.exists(path(fn)):
            frames, end = parse_opaque_list(open(path(fn), "rb").read()[base:], pal, "frame")
            for i, (kind, o, w, h, off, im) in enumerate(frames, 1):
                save(im, folder, f"{prefix}_{i:02d}_at{o:04X}.png")
            contact_sheet(frames, scale).save(os.path.join(outdir, f"{folder}_sheet.png"))
            print(f"sprites {fn} -> {folder}: {len(frames)} frames")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.isdigit()]
    opts = sys.argv[1:]
    here = os.path.dirname(os.path.abspath(__file__))
    scale = int(opts[opts.index("--scale") + 1]) if "--scale" in opts else 2
    palno = int(opts[opts.index("--palette") + 1]) if "--palette" in opts else 1
    pal = PALETTES[(palno, "--low" not in opts)]
    if args:
        game = args[0]
        outdir = args[1] if len(args) > 1 else os.path.join(here, "..", "Graphics", os.path.basename(os.path.normpath(game)))
        extract(game, outdir, scale, pal)
        return
    # default: every known release under ../Original_IBM_CGA, each into ../Graphics/<release folder>
    root = os.path.join(here, "..", "Original_IBM_CGA")
    for sub in sorted(os.listdir(root)):
        game = os.path.join(root, sub)
        if os.path.isdir(game) and detect(game):
            extract(game, os.path.join(here, "..", "Graphics", sub), scale, pal)


if __name__ == "__main__":
    main()

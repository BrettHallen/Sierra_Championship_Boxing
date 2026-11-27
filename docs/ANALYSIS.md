# Sierra Championship Boxing (IBM PC, CGA): how it works

*Sierra On-Line, 1984. Program by Dave Murry, animation by Barry Murry (the credits screen says "by Evryware").*
*Two IBM releases are covered: `Original_IBM_CGA/SierraCh` (a complete file-based build, **the recommended reference**) and `Original_IBM_CGA/1987_Crack` (a 1987 sector-slice conversion of the self-booting disk). See section 0.*

## 0. Which release to use

Both folders hold the **same game**, with the same engine, graphics and data formats. They were cracked into DOS files in two different ways.

| | `SierraCh` (KO.EXE) | `1987_Crack` (BOXING.COM + BOXING.OVR) |
|---|---|---|
| How it runs | `KO.EXE`, a file-based build. The tag inside it reads "Championship Boxing KO'd in 1984 by BIG BROTHER … Hello Disklooker!!!" | Bytten's 1987 loader fakes floppy sector reads from 30 slice files |
| Program | `KO.EXE` plus four overlays: `INTRO.OLY`, `GYM.OLY`, `FIGHT.OLY`, `CONFGR.OLY` | `BOXING.OVR` plus overlay pieces cut at sector boundaries (`OV2`–`OV5`, `GYM`, `FGT`, `FG2`, `FG3`, `OPT`, `OP1`, `MSG`, `MS2`) |
| Code | Same engine: 81% of `KO.EXE` appears verbatim in `BOXING.OVR`, and both carry the hidden `DPQZSJHIU 2:94 EBWF NVSSZ` copyright. The `.OLY` files use the same overlay header as `OV2`–`OV5` (`00 01`, size, `02 00`, size, `1A`); each is a whole overlay where the booter splits it across slices | — |
| Full screens | `OSIDE.PIX`, `GYM.PIX`, `RING.PIX`, `STAT.PIX`, `OPTION.PIX` | `PIC`, `PI2`, `PI3`, `PI4`, `PI5` |
| Boxer sprites | `BBXR0/1`, `WBXR0/1`, `CBXR0/1`, `KBXR0/1.SPT`: **all 8 sets** | 7 sets, **`BOXING.WR` missing** |
| Ring girl | `GIRL.SPT` | `BOXING.GRL` |
| Fight extras | Inside `FIGHT.OLY` at offset 22224 | `BOXING.FG3` |
| Boxer roster | `NAMES.DAT` plus one 42-byte `<NAME>.DAT` per boxer: **the stock roster of 60** (Sullivan, Corbett, Dempsey, Ali, Liston, Hearns… plus Sierra's joke entries such as Mr. Bill, Techy, Buh Buh) | `BOXING.NAM` and `BOXING.DAT`, edited by a previous owner (Tyson, Rocky, Ivan God…), with save dates of 1988 and 1998 |
| Options | `KO.CNF` (same fields as `OV1`) | `BOXING.OV1` |

**Byte comparison.** Every graphics file is identical, with one exception. `GYM.PIX`, `RING.PIX`, `STAT.PIX` and `OPTION.PIX` match `PI2`, `PI3`, `PI4` and `PI5` byte for byte. `GIRL.SPT` matches `GRL`. Each `.SPT` matches the used part of its `BOXING.xx` file: `xBXR0.SPT` = `BOXING.xR`, the left-side boxer, and `xBXR1.SPT` = `BOXING.xB`, the right-side boxer. The `.SPT` files simply stop where the sprite data ends (11,966 and 11,942 bytes), which confirms that the bytes after that in the `BOXING.*` files are sector slack. The one exception is the title screen. `OSIDE.PIX` and `BOXING.PIC` differ by about 3,000 pixels: the booter's version adds a "BY DAVE AND BARRY MURRY" line and moves the EXIT sign and some kerb lines. Nothing in the files says which revision came first.

**Recommendation: use `SierraCh` as the reference.** It is complete (it has the real white left-side boxer), its files have meaningful names and whole overlays, and its roster is the factory one. Keep `1987_Crack` as history: it is the best evidence of how the original booter disk was laid out (sections 1–2), and it has the alternative title screen. The extractor handles both.

## 1. The big picture (the booter disk, from `1987_Crack`)

The original IBM PC release was a **self-booting disk**. It had no DOS and no files. The game read raw sectors from the floppy with BIOS `INT 13h` (cylinder / head / sector), so the "files" in this folder never existed on the original disk.

What you have is a conversion made by someone signing as **Bytten**, dated 11 February 1987. You can read it in `BOXING.COM`:

```
Championship Boxing file loader - Version 1.0
11th February, 1987, by Bytten.
Hi to The Nybbler, Junkyard Dog, RAS, The Banshee, Phil Krakouer and all my other friends.
```

The trick behind it:

1. `BOXING.COM` (2,586 bytes) checks for DOS 2.0+, refuses to run from drive A:, shrinks its memory, and **hooks `INT 13h`**.
2. It then runs `BOXING.OVR` (the actual game) with DOS `EXEC`.
3. Whenever the game asks the BIOS to read or write a sector on drive A, the hook looks up the cylinder/head/sector in a hard-coded table. It opens the matching `BOXING.xxx` file and does the read (AH=2) or write (AH=3) with DOS file calls instead. Requests it doesn't recognise go to the real BIOS.

So each file is a **slice of the original 360 KB disk** (9 sectors × 512 bytes per track). Every file size is a multiple of 512. The game code was never changed. It still thinks it is talking to a floppy.

The game program itself also carries an obfuscated copyright, shifted by one letter: `DPQZSJHIU 2:94 EBWF NVSSZ` = `COPYRIGHT 1984 DAVE MURRY`.

## 2. What every file is (`1987_Crack`; SierraCh equivalents are in section 0)

| File | Size | Original disk location (cyl/head/sec) | What it is |
|---|---|---|---|
| `BOXING.COM` | 2,586 | n/a | Bytten's 1987 loader: hooks INT 13h and launches `BOXING.OVR` |
| `BOXING.OVR` | 40,192 | n/a (was the boot-loaded program) | **Main game program.** An MZ EXE wrapper around a `.COM`-style image (entry at 0100h). Holds the screen loader, sprite blitters, boxer animation engine, menus and fight logic. Most of the in-game text is in here (corner advice, injuries, key setup and so on) |
| `BOXING.OV1` | 512 | 5/1/3 | **Saved game options** (round length, mode, players, side…), written back by the game. Dated 1988, so it was saved during play |
| `BOXING.OV2`–`OV5` | 512 each | 13/1/5, 7/1/6, 10/1/2, 16/1/4 | Small code overlays, one sector each, with a short header and then x86 code |
| `BOXING.GYM` | 9,728 | 13/1/6 | Code overlay for **the Gym** (make, view, change or eliminate a boxer) |
| `BOXING.MSG` | 3,072 | 15/1/7 | Gym text (stat prompts, punch names, character types…) plus some code |
| `BOXING.MS2` | 1,536 | 9/1/7 | More text: options, validation messages, betting odds ("is a 3 to 1 favorite"), credits |
| `BOXING.OPT` | 2,048 | 16/1/5 | Code overlay for the **Options** screen |
| `BOXING.OP1` | 512 | 16/1/9 | Options-screen text |
| `BOXING.FGT` | 9,216 | 7/1/7 | Code overlay for **the Fight** (part 1) |
| `BOXING.FG2` | 10,752 | 10/1/3 | Code overlay for **the Fight** (part 2) |
| `BOXING.FG3` | 4,096 | 12/1/6 | **Graphics:** small fight-screen sprites (crowd pieces, ring corners, a face looking over the ropes…) |
| `BOXING.PIC` | 9,728 | 18/1/1 to 20/1/1 | **Full screen:** title / main menu (street with Gym, Fight, Options, Exit) |
| `BOXING.PI2` | 9,216 | 7/0/8 to 9/0/8 | **Full screen:** the Gym (trainer with cigar) |
| `BOXING.PI3` | 3,584 | 7/0/1-7 | **Full screen:** the arena and ring backdrop for the fight |
| `BOXING.PI4` | 3,072 | 17/1/2-7 | **Full screen:** End-of-Round Statistics and judges' scorecard frame |
| `BOXING.PI5` | 4,096 | 9/0/9 to 10/0/7 | **Full screen:** Options (with the ring girl) |
| `BOXING.GRL` | 9,728 | 33/0/2 | **Graphics:** ring girl animation (holds up the round card), scoreboard panel, "SIERRA BOXING" / "by EVRYWARE" logos |
| `BOXING.BB` / `.BR` | 12,288 each | 13/0/7, 10/0/9 | **Boxer sprites:** *Black* character. `R` = boxer on the left side of the ring, `B` = boxer on the right (from the decoded animation tables) |
| `BOXING.WB` | 12,288 | 24/0/8 | **Boxer sprites:** *White* character, right side |
| `BOXING.WR` | **missing** | 22/0/1 | *White* character, left side. **Not in this folder** (see below) |
| `BOXING.CB` / `.CR` | 12,288 each | 19/0/3, 16/0/5 | **Boxer sprites:** *Cartoon* character |
| `BOXING.KB` / `.KR` | 12,288 each | 30/0/4, 27/0/6 | **Boxer sprites:** *Kangaroo* character (yes, really) |
| `BOXING.NAM` | 1,024 | 6/0/1 | **Boxer names**, 9 bytes each (zero-padded), slot 0 empty. Written back by the game (dated 1988) |
| `BOXING.DAT` | 30,720 | 30/1/1 onwards | **Boxer database**: 60 records, one 512-byte sector each. Written back by the game (dated 1998) |

The character-type letters come straight from the Gym's own menu in `BOXING.MSG`: "1 - Black character, 2 - White character, 3 - Cartoon character, 4 - Kangaroo character".

### The missing `BOXING.WR` (found)

`1987_Crack` has no `BOXING.WR`, the **White** boxer on the **left** side. Bytten's loader stops with `FATAL ERROR: Cannot find BOXING.WR` whenever a fight needs it. None of the 29 other booter files contains it: its disk sectors (cylinders 22–24, head 0) overlap no other file, and the slack in the files only holds pieces of sets that already exist.

**The real art is in the SierraCh release as `WBXR0.SPT`.** It holds 28 frames (10 body, 11 head, 7 arm) with the same layout as `BR`, apart from one offset word: frame 2 uses 5 where BR has 4. `restored/BOXING.WR` is that file padded with zeros to 12,288 bytes. The padding matters, because Bytten's loader treats a short read as a fatal error. Copy it into a *copy* of `1987_Crack` to complete that release.

Before the real file turned up, `tools/reconstruct_wr.py` built an approximation (`reconstructed/BOXING.WR`) by learning Black→White from the right-side sets. Measured against the real art it gets **87.8%** of pixels right; simply copying BR gets 74.5%. It is now superseded, and kept only as a record of the experiment.

### Boxer names and records (`1987_Crack`; SierraCh uses the same 21-word record, one file per boxer)

`BOXING.NAM` holds 59 names. The first 31 are real heavyweight champions and contenders: Johnson, Willard, Tunney, Schmeling ("SCHMELNG"), Sharkey, Carnera, Baer, Braddock, Louis, Walcott, Marciano, Patterson, Terrell, Frazier, Foreman, Spinks, Norton, Holmes, Weaver, Coetzee, Page, Cooney, M. Frazier, Cobb, Hagler, Duran, Leonard, Curry, McCrory, Pryor. After those come boxers someone created over the years (Tyson, Rocky, Ivan God…). Past the names the sector holds leftover junk, including fragments of validation messages.

Each `BOXING.DAT` record lines up with a name (record *n* = name *n*+1) and uses
the first 21 little-endian words of its sector. The full field list, confirmed
from the Gym's statistics screen, is in `docs/GAMEPLAY.md` section 3 (it
replaces an earlier guess that had a few fields in the wrong order).

Record 4 (Sharkey) is all zeros, which looks like a boxer that was eliminated in the Gym.

## 3. Graphics formats

All the graphics are **CGA 320×200, 4 colours, 2 bits per pixel** (4 pixels per byte, leftmost pixel in the top bits). The game selects **palette 1, high intensity**: black, cyan, magenta, white. (The routine at OVR image offset `626Dh` calls `INT 10h AH=0Bh, BH=1`.)

CGA memory is **interlaced**. Even scanlines live at `B800:0000` and odd scanlines at `B800:2000`, 80 bytes per line. Every graphics format here is built around that split.

### 3.1 Full screens: `PIC`, `PI2`–`PI5` (SierraCh: `*.PIX`)

The loader is at OVR image offset `628Ch`. The format is **run-length encoded `(count, value)` byte pairs**:

- Pairs **alternate between the two banks**: one pair goes to the even bank, the next to the odd bank, and so on.
- Each bank fills up to 8,000 bytes (100 lines × 80 bytes). Once one bank is full, the remaining pairs all go to the other.
- A count of `0` is treated as 1.
- The data is read one 512-byte sector at a time, so pairs never cross a sector boundary. Whatever comes after the end of the image is sector slack.

The result is a raw 16 KB CGA screen. The simple screens compress very well: `PI4` is 2.6 KB against `PIC`'s 9.6 KB.

### 3.2 Sprites: boxers, `GRL`, `FG3` (SierraCh: `*.SPT`, `GIRL.SPT`, inside `FIGHT.OLY`)

Every sprite starts with little-endian 16-bit words:

```
width   (in 16-bit words; 1 word = 8 pixels)
height  (in line PAIRS; each row covers one even + one odd scanline)
offset  (signed screen offset, used to position the piece relative to the boxer)
```

The pixel data is stored one row at a time. Within a row it goes word by word, alternating banks: `[even-line word][odd-line word][even word][odd word]…`. That is exactly the order the blitter (`6A91h`) writes to `ES:[DI]` and `ES:[DI+2000h]`. A sprite of `w × h` is therefore `(w·8) × (h·2)` pixels.

There are three flavours, defined by which part of the drawing routine uses them:

| Flavour | Header | Data | Used for |
|---|---|---|---|
| **Opaque** | 6 bytes | `4·w·h` bytes, then 2 padding bytes | Boxer lower bodies and legs, knock-down and KO poses, ring girl, logos, FG3 pieces |
| **Head** | 8 bytes (adds a second offset word) | `4·w·h` bytes, always 6×20 (48×40 px) | Boxer heads and upper bodies |
| **Masked** | 6 bytes | Per row: the first and last word columns are `(AND-mask, OR-data)` pairs for each scanline, and the middle columns are opaque. Row = `4·w + 8` bytes | Arms and punches, laid over the body |

Opaque sprites have the **ring background baked in**: the solid cyan canvas, and the black/cyan stripes of the ropes and crowd. Only the punching arms are truly masked, so they can swing over whatever is behind them.

### 3.3 How a boxer is drawn

The boxer animation routine is at OVR image offset `66A3h` (`CS:67A3`). The full format of its tables and sequences, with the addresses for both releases, is in `docs/GAMEPLAY.md` section 11. On every frame it:

1. Waits for vertical retrace (port `3DAh`), with an extra delay on PCjr/Tandy-style machines it detects through `INT 11h`.
2. Reads a 5-byte animation step. That holds a horizontal move, three indexes into tables of **head**, **arm** and **body/legs** sprites, and a state byte.
3. Restores the background where the boxer used to be. It doesn't save the screen; it just repaints the ring's background pattern (`5555h` cyan floor, `1111h` stripes, `FFFFh` rope) depending on which screen rows it is touching.
4. Draws head (opaque) at the boxer's position, then arms (masked) at the head's first offset minus the arm's own offset, then body and legs (opaque) at the head's second offset minus the body's own offset.

That is why each boxer file holds three sets of pieces: about 10 body/leg frames (including the knock-down and KO poses), 10–11 head frames and 7–9 arm frames. Every punch and stance is built by mixing them.

## 4. Extracting the graphics

`tools/extract_graphics.py` (Python 3 + Pillow) only **reads** the original files. It recognises either release by its files, and with no arguments it extracts every release folder under `Original_IBM_CGA/` into `Graphics/<folder>/`:

- `screens/`: the 5 full screens, numbered (1 title, 2 gym, 3 arena, 4 stats, 5 options)
- `boxers/<set>_<character>_<side>/`: every body, head and arm frame as PNG (masked arm edges are transparent), plus a contact sheet per set. SierraCh has all 8 sets; 1987_Crack has 7
- `ring_girl_and_logos/` and `fight_extras/`, plus contact sheets

```
cd tools
python3 extract_graphics.py                                  # all releases, 2x scale
python3 extract_graphics.py ../Original_IBM_CGA/SierraCh      # one release
python3 extract_graphics.py --scale 1                        # native pixels
python3 extract_graphics.py --palette 0                      # green/red/yellow palette for fun
python3 extract_graphics.py --low                            # low-intensity colours
```

## 5. Loose ends worth exploring

- ~~**Rebuilding whole boxer poses.**~~ Done: the tables and all 27 sequences are decoded in `docs/GAMEPLAY.md` section 11, and the remake draws poses from them.
- **The last ~300 bytes of each boxer file** (after offset `2EA6h`/`2EBEh`) and the tails of `GRL` and `FG3` don't parse as sprites. They look like leftover disk content.
- ~~The exact order of the 1–4 rating fields in `BOXING.DAT`~~ Done: see `docs/GAMEPLAY.md`.

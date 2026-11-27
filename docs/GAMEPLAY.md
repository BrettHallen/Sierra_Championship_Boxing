# Sierra Championship Boxing: how the game plays (reverse-engineered)

This document explains the game logic of the 1984 IBM PC (CGA) version: how the
program is laid out in memory, how a boxer's Gym answers turn into fight
numbers, what the corner advice actually does, how the judges score, and how
injuries, fatigue and stoppages work, and how the boxers are animated. It builds on `docs/ANALYSIS.md`, which
covers the files and graphics formats.

Everything here was worked out by disassembling `BOXING.OVR` and the code
overlays. Addresses are given so you can check them yourself with the tools in
`tools/re/` (see the end). Where a conclusion is a reading of the code rather
than something I could pin down exactly, it says so.

**Confidence key:** ✅ read directly from the code · 🟡 strong inference · ❓ guess / still open

---

## 1. Memory layout and overlays ✅

`BOXING.OVR` is an MZ wrapper around a small-model program:

| | Where | Notes |
|---|---|---|
| Code segment (CS) | image loaded at `CS:0100` | entry `CS:0100 → jmp 6C0E` |
| Data segment (DS = SS) | file offset `0x200 + 0x75A0` → `DS:0000` | so `CS:x = DS:(x − 76A0h)` for data stored in the file |
| BSS | above the file data | boxer records, fight state, options live here (`DS:8xxx`) |

The program was written in a compiled language with a recognisable runtime:
every function starts with `call 6CAB` followed by one byte of "local-variable
size" (or `call 6CBE` + a word, or `call 6CA6` for none). Arguments are pushed
right to left, results come back in `AX`. The fight engine itself
(`FG2` `16EE`–`29E8`) is hand-written assembly that keeps the attacker in `SI`
and the defender in `DI` (each ×2, indexing word arrays of two boxers).

### Overlays

The low part of memory (`CS:0100`–`CS:2B00` for code and `DS:0002`–`DS:0Axx`
for data) is an **overlay area**. Each part of the game is read into it from
disk when you enter that part. Each overlay starts with a 16-byte header:

```
+0  code load address  (always 0100h)
+2  code size
+4  data load address  (always 0002h)
+6  data size
+8  1Ah, then FFh padding
```

Code follows the header, and the data (strings, sprite tables) follows the
code. The loader (`OVR 3A69`) reads the first sector, copies 1F0h bytes, and
reads the rest straight to the destination.

Because the game reads consecutive sectors **on head 1 only**, an overlay runs
across several of the 1987 loader's files. Put together:

| Overlay | Files (in order) | Code / data size | What it is |
|---|---|---|---|
| Gym | `OV2` + `GYM` + `MSG` | 26B0h / 0A06h | Make, view, change, eliminate boxers. `MSG` is its string table |
| Fight setup | `OV3` + `FGT` + `MS2` | 24D0h / 0576h | Pick boxers, tale of the tape, betting odds, credits |
| Fight | `OV4` + `FG2` + `FG3` | 2A00h / 0E7Ch | The rounds themselves. `FG3` is its data: ring sprites + animation tables |
| Options | `OV5` + `OPT` + `OP1` | 08E0h / 01FAh | Options screen. `OP1` is its strings |

That explains why `FGT` starts mid-instruction and `MSG` starts mid-word: they
are continuations, not standalone files.

### Useful runtime routines (main program)

| Address | Routine |
|---|---|
| `60D4` | `disk_io(op, buffer, cylinder, sector, use_ds)` – INT 13h, head from `[1DF4]` |
| `6203` | **`random()` → 0…127** (see below) |
| `61ED` | seed the random generator from the BIOS tick count |
| `62EA` | `delay(n)` – calls `random()` n times (halved on PCjr) |
| `638C` | load an RLE full screen |
| `61B3` | `gotoxy(col,row)`; `66DB` puts; `66C8` putch; `3705` print number |
| `636D` | select CGA palette 0 or 1 |
| `6526`/`6562` | install / remove the INT 9 keyboard handler used during rounds |
| `6472` | ring the bell through the PC speaker (section 13) |
| `6BDC` | `switch` statement dispatcher (value list + relative jump table after the call) |

**The random number generator ✅** is unusual: it walks a pointer through the
game's own code bytes (`CS:03E8`…`CS:2328`), adds each byte to a running total
and keeps the low 7 bits. The start value comes from the BIOS timer. So every
"chance" in the game is a comparison against a number from 0 to 127, and
ratings are on that same scale.

---

## 2. Options ✅

The options live in one sector (`BOXING.OV1`, disk 5/1/3) that is loaded to
`DS:818C` at start-up and written back when you leave the Options screen.

| Offset | Word | Meaning | Values |
|---|---|---|---|
| +00 | `818C` | Sound | 0 off, 1 internal speaker, 2 external |
| +02 | `818E` | Colours | 0 set 1, 1 alternate each round, 2 set 2 (palette 1 / palette 0) |
| +04 | `8190` | Side the human plays (1-player) | 0 left, 1 right (`FGT 1335`, `OPT 0402`) |
| +06 | `8192` | Number of players | 0, 1, 2 |
| +08 | `8194` | Number of rounds | 1–15 (default 12) |
| +0A | `8196` | Scoring | 0 ten-point must, 1 rounds won (`OPT 02B6`, `FG2 2ABF`) |
| +0C | `8198` | Round length | 180 = 3 min, 60 = 1 min (older value 1 also means 3 min) |
| +0E | `819A` | Bell | 0 beep, 1 ring |
| +10 | `819C` | Intro music | 0 off, 1 on |
| +12 | `819E` | Mode | 0 Simulation, 1 Strategy, 2 Arcade |
| +14 | `81A0` (byte) | Screen centring | CRTC register 2 value, 42–51 (default 45) |

If the sector can't be read, the defaults are sound on, set 1, 2 players… 12
rounds, 3-minute rounds (`OVR 35FE`).

Brett's saved `OV1`: internal sound, alternating colours, 1 player on the right,
5 rounds, rounds-won scoring, 1-minute rounds, ringing bell, intro on, Arcade mode.

### The three modes

* **Simulation** (0 players): the computer fights both boxers. You watch, and
  you may be offered the corner screens.
* **Strategy**: you play a boxer's corner. Between rounds you give advice
  (section 5); during the round the computer fights.
* **Arcade**: you control the boxer live with the keyboard (an INT 9 handler
  reads keys directly). Keys are re-mappable from the corner screen ("Press O
  for arcade mode options"): No Action, Jab, Uppercut, Head Punch, Body Punch,
  Duck, Cover Head, Cover Body, Step Forward, Step Back. You also choose a
  **fighting style** (Slugger or Boxer) and your **reflexes** (very fast / fast
  / average / slow).

`[1D00]` and `[1D02]` flag the left and right boxers as human-controlled
(`FGT 1300`).

The Options screen (`OPT 014C`) prints one option per text row, rows 3 to
12 at column 3 (option 10 at column 2), and asks "SELECT ONE OF THE ABOVE
OPTIONS: __" on row 24. Option 1 centres the screen with A and S; 11 is the
EXIT sign drawn on the picture. In Simulation mode the players line is blank.

---

## 3. Boxer records (`BOXING.DAT`) ✅

Each record is one 512-byte sector, read to `DS:8431`. This corrects the field
order guessed in ANALYSIS.md, using the Gym's own "STATISTICS" screen
(`GYM 1E2x`), which prints each field next to its label:

| Word | Field | Range / values |
|---|---|---|
| 0 | Times knocked out (KO defeats) | |
| 1 | Reach, inches | 40–99 |
| 2 | Height, inches | 60–99 |
| 3 | Character | 1 Black, 2 White, 3 Cartoon, 4 Kangaroo |
| 4 | Age | 17–50 |
| 5 | Weight, lb | 70–350 |
| 6 | Fights | |
| 7 | KOs of opponents | |
| 8 | Absorbs a punch | 1 almost never … 4 almost always |
| 9 | Best punch | 1 Jab, 2 Uppercut, 3 Overhand right, 4 Left hook, 5 Body, 6 Right hand, 7 Left hand, 8 None |
| 10 | Is injured / cut | 1–4 |
| 11 | Goes in for the kill (killer instinct) | 1–4 |
| 12 | Uses good defence | 1–4 |
| 13 | Hits what he aims for (accuracy) | 1–4 |
| 14 | Losses | |
| 15 | Hitter | 1 light, 2 average, 3 hard |
| 16 | Starter (warms up) | 1 fast, 2 average, 3 slow |
| 17 | Is aggressive | 1–4 |
| 18 | Tires during | 1 first few rounds, 2 middle, 3 last |
| 19 | Uses fast punches (hand speed) | 1–4 |
| 20 | Update record automatically | 0/1 |

The disk locations of the records come from a table in the fight-setup overlay
(`FGT 07B8`, a cylinder/sector pair per boxer number), and `BOXING.NAM` (disk
6/0/1) holds the 9-byte names.

---

## 4. From Gym answers to fight numbers ✅

When a boxer is picked for a corner, `FGT 03A0`–`06DA` and `FGT 13BC`–`1500`
turn his record into a set of **base ratings** (`DS:1D70`…`1D9E`, interleaved
for the two boxers). At the bell for round 1 (`OVR 3330`) they are copied to
**live ratings** (`DS:8405`…`842A`) which change during the fight and are reset
or nudged between rounds.

| Live | Base | Formula from the record | What it drives |
|---|---|---|---|
| `8405` | `1D70` | 15·accuracy + 10·hand speed + fights − losses | **Skill / accuracy**. Corner says "Don't throw wild punches, aim!" when it is under 80 |
| `8409` | `1D74` | 20·aggressiveness | **Aggression**: with `83BF` it sets how often he attacks |
| `840D` | `1D78` | 65 − 10·defence | **Openness**: chance a punch he was not ready for gets through his guard (section 7) ✅ |
| `8411` | `1D7C` | 10·defence | **Defence** |
| `8415` | `1D80` | 110 − fatigue − weight/10 + 20·tires | **Energy** (see section 6) |
| `8419` | `1D84` | fights / (KO defeats + 1) + 4·absorbs | **Toughness / chin**: chance per try of getting up from a knock-down ✅ |
| `841D` | `1D88` | 10·hitter + 60·KOs / (fights + 1) | **Power** |
| `8421` | `1D8C` | 100 if best punch is "right hand", else 80 | **Jab threshold**: the computer throws an uppercut when `random() >` it, else a jab ✅ |
| `8425` | `1D90` | 3·(5 − absorbs), doubled if age > 36 | **Vulnerability**: chance a clean shot knocks him down ✅ |
| `8429` | `1D94` | 45 if best punch "none", 65 if jab, else 85 | **Head vs body** targeting ✅ |
|  | `1D98` | 127 − injury-prone rating | Injury threshold (`random() > 1D98` is one of the rolls for an injury) ✅ |
|  | `1D9C` | killer instinct 1→40, 2→70, 3→90, 4→110 | Chance of a follow-up flurry on a stunned opponent |
|  | `1DA4` | age/2 + weight/5 − 38 + 4·(3 − tires) | **Fatigue rate** (overwrites the age copy) |
| `8401` |  | starter rating | Slow starters fight at reduced pace early 🟡 |

Other per-fight values: `09A9` weight, `09AD` height, `09B1` fights, `09B5`
losses, `09B9` KOs (tale of the tape), `83F7` best punch, `842D` character.

### Adjustments for the pair (`FGT 13BC`–`17D2`) ✅

Once both boxers are picked:

* **1-minute rounds:** each boxer's vulnerability ×4 and injury threshold −1.
* Each boxer's vulnerability goes up by 3 × the **other** man's hitter rating
  (`FGT 06C4`, `152D`).
* **Weight:** if one man is more than 25 lb heavier, `d` = the difference −
  25 (a quarter of that if the heavy man is over 235 lb, nothing over 280).
  The lighter man gets vulnerability +d and openness +d; if the heavy man is
  under 235, the lighter man's toughness is divided by 3 (`d > 20`) or 2.
  The aggression change has a bug: it is meant for the lighter man, but both
  branches lower `1D76`, the right boxer's (so a heavier right boxer loses
  aggression, and a heavier left boxer takes it from the lighter right one).
* **Reach:** more than 5 inches longer: `1DA0`/`1DA2` = 1 (he punches from 1
  byte farther), his head targeting +20 and the other's −20.

The remake applies these in `fight_init`, including the bug.

Two things stand out. Experience matters a lot: *fights − losses* is added
straight onto skill, so a 99-fight veteran is far more accurate than a novice
with the same Gym answers. And age only hurts through fatigue and through the
doubled vulnerability after 36.

**Betting odds** ("X is a 3 to 1 favourite", `MS2`) are shown on the tale of
the tape and compare the two boxers' ratings 🟡.

---

## 5. Between rounds: the corner ✅

`OVR 4867` shows "*name*'s corner" and the status of both men, then (in
Strategy, or for a human's boxer) offers advice. Each line is only offered when
it makes sense, and each one has a fixed effect. The key handler is the
`switch` at `OVR 5408`:

| Key | Advice shown | Shown when | Effect on your boxer |
|---|---|---|---|
| **Q** | "Punch to his head" / "Work on that nose/eye" | always (wording depends on the opponent's injury) | head targeting +20 (to 100 max), defence −5, jab threshold −15 |
| **W** | "Hit him in the ribs" / "Work on his body" | always | head targeting −20 (to 30 min), defence −5, jab threshold reset |
| **E** | "Duck, watch out for his *best punch*" / "Don't let him get to your ribs" / "Cover up, protect that nose/eye" / "Fight defensively, rest up" | depends on opponent's targeting and your injuries | **defensive round**: aggression −70, openness −15, defence +40, energy +30, vulnerability −10, momentum −40; clears your injury unless your eyes are glazed |
| **A** | "Don't throw wild punches, aim!" / "Jab, keep him away" / "Back off, wait for an opening" | *aim* when skill < 80 (and a 40/128 roll), *jab* when the jab threshold is low (65/128 roll), else *back off* | *Jab*: skill +10, jab threshold +15, head targeting +10. *Aim* and *back off* share one effect: aggression −40, openness −10, defence +25, energy +20, momentum −20 |
| **S** | "We're behind, nail him" / "He's a goner, finish him" / "Go for the knockout" | depends on score and opponent's energy | **all-out round**: aggression = 115, openness = 40, defence = 10, vulnerability +5, momentum +60, energy −20 |
| **D** | "Dump ice water down his pants" / "You're doing good, Champ" | not round 1 | aggression +10, energy +10, vulnerability −5, defence reset to base |
| **X** | "Take a dive, we lost anyway / need the money" | not round 1 | vulnerability = 120, openness = 100, toughness = 0 (he *will* go down) |
| **C** | "Let me put ice on your eye" / "Let me stop the bleeding" | only with an eye or bleeding injury | injury one step less serious, skill +5, aggression +10 |
| **O** | Arcade key and style options | | |

The flags `83A3` (all-out) and `83A7` (defensive) undo themselves at the end of
the next round (`FG2 0A91`): after an all-out round aggression drops to base − 25,
openness to base + 5 and defence to base − 5 (he has punched himself out); after
a defensive round all three go back to base.

The computer corner (`OVR 4A00`–`4B00`) uses the same table to choose its own
advice from the fight situation: it is behind on points, the opponent is
exhausted, it is the last round, and so on.

---

## 6. Energy and fatigue ✅

`8415` is energy. The status line ("*name*'s status –", `FG2 15FF`) reads it:

| Energy | Status |
|---|---|
| > 90 | Breathing heavy. |
| 66–90 | Noticeably tired. |
| 36–65 | Very tired. |
| ≤ 35 | Exhausted. |

At the end of each round (`FG2 097F`):

```
energy -= damage_taken_this_round / 6          (a knock-down adds 75 damage)
if energy < starting energy and rounds are 3 minutes:
    energy += recovery[round]   (15 for rounds 1-6, 10 for 7-11, 6 for 12-15)
else:
    energy -= 5
if a human is playing and energy is still under the starting value: energy += 20
```

Energy **does not change during a round** ✅. Punches add to the damage
counter `83FD` (section 7), and that only comes off at the end of the round, so
the status line only moves between rounds. A boxer with a high starting energy
shows "Breathing heavy." for most of a fight; one who starts at 66–90 shows
"Noticeably tired." from the first bell. That is how the original behaves.

Energy also feeds the corner's choices: "Dump ice water…" when yours is under
45, "He's a goner…" when the opponent's is under 35.

---

## 7. Inside a round ✅

The round is `FG2 16EE`–`248E`. It has no timer of its own: it is one loop,
and **each pass is one frame of one boxer**, the two boxers taking turns
(`83C7` is the boxer whose pass it is, `83C9` the other). Everything that
happens in the ring is driven by the animation sequences of section 11: a
punch can only land on the frame whose state byte says it lands, and the
computer only decides what to do next when its current sequence has run out.

**The state byte.** The fifth byte of every frame (section 11) is copied to
`0EEC`/`0EEE` when the frame is drawn. Its high nibble says what is open,
its low nibble what is happening:

| High nibble | Meaning | Low nibble | Meaning |
|---|---|---|---|
| `00h` | covered (crouch) | `2` | a head punch is landing on this frame |
| `10h` | head open | `5` | a body punch is landing on this frame |
| `20h` | body open (glove up at the face) | `7` | hurt (the "rocked" frames) |
| `30h` | head and body open (punching, stepping) | `8` | hits the canvas (`48h`, a thud) |
| `40h` | down | | |
| `50h` | staggering | | |

The meaning of `2`, `5`, `7` and the high nibbles is read from the tests
below (✅). The words "covered" and "open" are mine (🟡).

### One pass (`FG2 1779`)

For the boxer whose pass it is ("he"), with the other man's last frame:

1. **Threshold.** `83FB = momentum + aggression`, at least 15.
2. **Does a punch reach him?** Only if the distance `0E3D` (right position −
   left position, in CGA bytes) is under 16 (17 with a human in the fight),
   and he is not down or staggering. Then:
   * a **head punch** lands if the other man's frame is `x2`, his own last
     frame was head-open (`1x` or `3x`), and `random() < the puncher's skill`
     (in a fight with a human this roll is skipped);
   * a **body punch** lands if the frame is `x5` and he was body-open (`2x`
     or `3x`). There is no skill roll.

   If neither, skip to step 4.
3. **What the punch does** (`FG2 1815`). First a point goes on his counter
   (`1D10`/`1D12`, see section 9). In a fight with a human, 3 more, and an
   Arcade style roll: unless he is a human with an equal or quicker style, it
   is cancelled on `random() > 50` (and again on `random() > 50` if the
   puncher's style is 2 or more higher). A cancelled punch takes the point
   back off and does nothing.

   * **Was he ready for it?** `random() > threshold` means he was:
     * on the last step before the ropes (`0B9B`/`0BCA`): a body punch, or
       `random() < 100`, leaves him standing; otherwise he **staggers on the
       ropes** (`0EC2`, state `50h`, 3 more points against him);
     * one step further in (`0B9C`/`0BC9`): `random() < 100` blocks, otherwise
       he staggers (`0EC6`, 3 more points);
     * elsewhere: a head punch with `random() ≤ 40`, or any punch to a human,
       is blocked; otherwise he backs off (`2499`).
     * "Blocked" (`18C1`) is the glove up at the face for a head punch, the
       standing guard for a body punch, or a stance frame for a human.
   * **Not ready:** `random() ≥ openness` still blocks. Otherwise roll
     `r = random() + 8·(higher Arcade style − 1)`:
     * `r < 85`: a **glancing blow**. He stands (body) or snaps back (`0E8A`,
       head).
     * `r ≥ 85`: a **clean shot**. It is a **knock-down** if
       `random() < vulnerability`, or if he was already hurt (last frame
       `x7`) and `random() < 80`. Otherwise it may **hurt** him (next).
   * **Hurt** (`FG2 1A5E`): only away from the ropes (position strictly
     between `0B9D` and `0BC8`), and only if `random() > 2·energy − the
     puncher's power`. Then aggression −2, momentum −40 (−80 if also
     `random() > energy`), toughness −1, vulnerability +1, openness +1, 8
     points against him, the "rocked" sequence `0E96`, and the puncher's
     momentum jumps to 60 if `random() < his killer instinct`. If he is not
     hurt: a body shot makes him crouch (`0EAE`), a head shot snaps his head
     back (or blocks, for a human).
   * Every punch that reaches step 3 and is not cancelled, blocked or not,
     then goes through **damage** (below).
4. **His sequence has run out** (`FG2 1BD3`): if he is down, the count rules
   below choose his next sequence. Otherwise the computer chooses (below),
   then a human's key replaces that choice, then the rope rules.
5. **Draw his next frame**, store its state byte, recompute the distance, and
   hand over to the other boxer.
6. **The clock** (`FG2 2161`), every 15 passes (below).

### Damage and injuries (`FG2 1ADB`) ✅

Every punch that reaches him adds to `83FD`, the damage that comes off his
energy at the end of the round (section 6): 2 for a head punch, 6 for a body
punch. Then, at most once per round for the two men together (`0E6F`):

```
if (random() > defence + energy − puncher's power   or he is going down)
   and random() > injury threshold (1D98)
   and random() > 40:
    skill −5, openness +5, damage +20, toughness −1, vulnerability +2
    injury already 2, 4, 6 or 8 (the bad ones):
        toughness −4, skill −10, aggression −40, vulnerability +10
        if random() > 65: the referee stops the fight (TKO, he loses)
    injury 1, 3, 5 or 7: it gets worse by one (cut → gash, bruised → broken rib,
        cut nose → bloody nose, swollen → shut eye)
    no injury (or glazed eyes): a body punch gives a bruised rib; a head punch
        gives a slight cut (random() > 80), else a cut nose or a swollen eye
        depending on the random generator's last value (above 40: nose)
```

So a fight is only stopped for an injury that is hit again after it has
become bad, never at random. The corner's **C** advice takes a bad injury
back down a step.

### Knock-downs (`FG2 1969`) ✅

* knock-downs +1 (this round and in total), damage +75, aggression −10,
  130 points against him, toughness −3, vulnerability +2, openness +2;
* if `random() < the puncher's killer instinct`, the puncher's momentum jumps
  to 60;
* if `random() − 30 > his energy` and he is not injured, he gets **glazed
  eyes** (injury 9): aggression 10, openness 65, vulnerability +10;
* his momentum drops to −40, his canvas timer `0E63` is set to 16;
* the puncher steps back unless he is on the ropes;
* if he was pushed past the centre of the ring (`0BB3`) he falls **flat**
  (`1D0C` = 2), with a longer fall near the ropes; otherwise he falls onto his
  seat (`1D0C` = 1).

### The count and getting up ✅

The referee's count (`83D5`) starts at the next clock period after anyone goes
down. After three periods it goes up by one every period (15 passes), and it
is shown as the count − 1, so the first number is 1.

Each time the downed man's sequence ends (`FG2 1BF7`), his canvas timer goes
down by one. He **can move up a stage** (flat → sitting with the timer at 16,
sitting → getting up) when all of these hold:

* toughness > 0;
* `random() < toughness`, or the bell has gone in the last round;
* fewer than 3 knock-downs this round (**the three-knock-down rule**);
* the count has started to run (`83D5 > 1`);
* his timer has run out.

A man sitting up who does not rise falls back flat on `random() > energy`
(timer 20). Once he is up, both men wait (the computer sends the standing man
to a neutral corner) until the count reaches 9, a mandatory eight count. If
the count reaches 11 (shown as 10) with a man still down, it is a **KO**
(`1CFE`).

### The computer's choices (`FG2 1C98`) ✅

When his sequence has run out and he is standing:

* **The other man is down, a count is running, or the bell has gone:** away
  from the ropes he takes two steps back (to a neutral corner), otherwise he
  waits (`random() > 30`: 3 frames of guard, else stance).
* **Too far** (distance ≥ 16, or 17 with the longer reach, `1DA0`), or he is
  human: he **closes in** if `random() < threshold` (or the other man is
  hurt), he is not human, and the other man is not staggering: two steps if
  the distance is over 24, else a shuffle (`random() > 40`) or a step.
  Otherwise stance.
* **The other man is staggering:** he backs off (`2499`).
* **Attack** if `random() < threshold / 2`, or the other man is human, or the
  other man is hurt and `random() < threshold`. Near the end of the round
  (clock step 16 and the last period of it) he defends instead.
  * `random() < fatigue rate`: damage +1 (punching tires him).
  * **Head** if `random() < head targeting`: at a distance under 15 he backs
    off instead; otherwise an uppercut (`random() > jab threshold`) or a jab.
  * **Body** otherwise: under 15, by `r = random()`: crouch-and-punch
    (`r > 100`), body punch A (`> 60`), body punch B (`> 30`), or the hook.
    Farther out he closes in.
* **Defend** otherwise: `random() < openness`: he stands (1 or 3 frames).
  Else `random() < defence`: crouch away (`r > 100`), glove up (`> 60`) or a
  short crouch. Else, `random() > threshold` away from the ropes: back off.
  Else stance.

Then, for every boxer whose sequence ran out:

* **Too close** (distance < 13): the man away from the ropes backs off.
* **On the ropes** (`0B9B` or less on the left, `0BCA` or more on the
  right): a computer boxer crouches out at a distance under 17 with
  `random() > 70`, otherwise he shuffles forward.

**Back off** (`FG2 2499`) is two steps back from beyond the centre, a short
crouch on the ropes, and one step back in between.

### Arcade keys (`FG2 1E70`, `28CE`) ✅

A human's key is used when his sequence runs out, and never while a man is
down, a count runs or after the bell. The commands are in section 11. A punch
key also rolls the fatigue damage like the computer, and **puts one point on
the human's own counter**, so it counts for the opponent (✅ as written; why
is a guess: wild punching should not win rounds). A computer opponent may then
react: at a distance under 17, if `random() < his defence` and he is not down
or staggering, a glove up or crouch against a head punch, or a guard against a
body punch.

### The clock (`FG2 2161`, `2408`) ✅

Every 15 passes is a **period**. Each period:

* the clock moves on; 9 periods (3 for 1-minute rounds, `8397`) make one of
  its 17 steps (`1D1E`);
* with `random() < 20` the next boxer's pace changes: momentum 60 if
  `random() < energy` (damage +5), else −65 (`random() > 80`, damage −15),
  else −30;
* the count runs (above);
* after 17 steps the **bell** goes, unless a man is down in a round other than
  the last (no saving by the bell, except in the last round). The round ends
  5 periods after the bell, once nobody is down.

So a round is 17 × 9 × 15 = 2295 passes (765 for 1-minute rounds): about
1150 frames per boxer.

**Speed** 🟡. There is no timer in the loop, but every pass ends with a pause
(`FG2 24EA`): 800 calls of the random routine (`OVR 62EA`), or a punch sound
instead of it. The frame drawers (`OVR 67A3`, `6A85`) wait for the vertical
retrace first. On a 4.77 MHz PC one call is roughly 45 µs, so a pass is about
36 ms of pause plus the drawing, rounded up to the next retrace: about 13
passes a second. The remake runs 12.75 passes a second, which also makes a
round last its nominal 3 minutes (or 1). Brett's DOSBox recording (default
cycles) ran a 1-minute round in about 26 s, roughly 30 passes a second: DOSBox
is fast, as he says, and its pauses are even faster (the judges' cards, two
pauses of 25000 + 10000 calls each, turn over in about 0.15 s instead of
about 1.5 s).

### Round start (`FG2 0845`, `27E2`, `2857`, `16EE`) ✅

The count, clock, bell and canvas timers are cleared, then:

* **Round 1** (`27E2`): both men stand in the middle (`0BAA`/`0BBA`), one
  standing frame each (`IDLE_LONG`), pause 20000. They **touch gloves**: one
  frame each that is in no slot (1987 `0BD1`/`0E37`, found by the code that
  draws them), the tap sound (`8399` = 1), pause 15000. Then `2762`: 35 frames
  each, alternately, of the standing sequence, and whenever it runs out
  `2499` (back off) picks the next, so they walk back to their own halves;
  pause 5000.
* **Last round** after the first (`2857`): from `0BA6`/`0BBE` they take four
  frames each of "two steps forward" (pause 1600 per pair), touch gloves, tap,
  pause 15000.
* **Other rounds**: from the corners (`0B9B`/`0BCA`).

Then the bell (`089C`), and the round loop (`16EE`) starts.

There is no separate round card. `FG2 0100` prints "ROUND n" at (17,10) (or
"LAST ROUND" at (16,6) and the number at (23,10)) on whatever screen is up,
and `023A` loads the ring straight away, so it only flashes. Brett's recording
shows no card at all.

**The scoreboard** (`FG2 130B`). The ring picture has two black panels. The
names and the running totals are printed with the BIOS at text columns 83 and
107 (names, row 1) and 46 and 70 (scores, row 3). In 40-column CGA graphics
the BIOS just adds 2 bytes per column, so these land one or two text rows
lower and to the right: names at pixels (24,12) and (216,12), scores at
(48,26) and (240,26). A name is centred in 8 characters, with one more space
when the score has three digits. The totals are `83CB`/`83CD` (section 9).
During the round only "ROUND"/"COUNT" and a number are printed under the
ring.

---

## 8. Injuries and stoppages ✅

Injury levels (`1D14` per boxer) match the strings at `OVR 47A4`:

| # | Text | # | Text |
|---|---|---|---|
| 0 | Not hurt. | 5 | Cut on nose. |
| 1 | Slight cut over eye | 6 | Bloody nose. |
| 2 | Bad gash over eye. | 7 | Swollen eye. |
| 3 | Bruised rib. | 8 | Eye swollen shut. |
| 4 | Broken rib. | 9 | Eyes are glazed. |

The **referee stops the fight** (`FG2 1B98`, shown by `0C46`) when a boxer
with a bad injury (2, 4, 6 or 8) is injured again and `random() > 65`
(section 7). The message depends on the injury:

* 2 (bad gash) or 8 (eye swollen shut): "*name* can not see!"
* 4 (broken rib): "*name* is bent over in pain!"
* 6 (bloody nose): "*name* has a hard time breathing!"

That is a **technical knockout**. The corner's **C** advice ("put ice on your
eye / stop the bleeding") takes one step off an injury, which is how you keep a
cut man from being stopped.

---

## 9. Judges and scoring ✅

After each round the three judges score it (`FG2 29E8`, called for judges 1, 2
and 3):

```
for each point against the RIGHT boxer (1D12):  if random() < 95 (65 in 3-min rounds): a += 1
for each point against the LEFT boxer (1D10):   if random() < 95 (65 in 3-min rounds): b += 1
with chance 40/128: give one side 30 bonus points (65/128 left, 63/128 right) – judge bias
pad both up to at least 10
scale so the higher count becomes 10:  a' = a·10 / max(a,b) / ... ;  b' likewise
round score = table[a'], table[b']  with table = 7,7,8,8,8,9,9,9,9,10,10
in "rounds" scoring, divide by 10 (so the round winner gets 1 and the loser 0)
```

`a` is the left boxer's score: it is printed on the row of his name (`FG2
14D9`, row 19), so `1D10`/`1D12` count *against* each boxer ✅. Points go on a
man's counter when a punch reaches him (1, or 4 with a human in the fight),
when he staggers (+3), is hurt (+8) or is knocked down (+130), and when a
human throws a punch (+1 on his own).

So in **ten-point-must** scoring the better boxer gets 10 and the other gets
7–10 depending on how close it was. Each judge keeps his own card
(`83AB`/`83B3`). The totals on the scoreboard (`83CB`/`83CD`) add up **all
three** judges' cards (`2AE3`): 7 + 7 + 7 = 21 against 30 after round 1 in
Brett's recording. In rounds scoring, `13E0` then gives 1 to whoever got more
from the three judges this round (nothing on a tie), so the totals count
rounds won. The corner's "we're behind" advice compares these totals. If the
last round ends with the cards level, it is "THIS FIGHT IS A DRAW!". The
random check and the bias mean split decisions really happen.

**End-of-round statistics** (`FG2 1462`, on `STAT.PIX`/`PI4`, palette 1):
"*name*'s status -" at (0,4) and (0,9) with the energy text at column 20 and
the injury below it; the names at (1,19) and (1,21) in the table, the round
number at (27,15). Each judge's card is turned over in turn: pause 25000, the
left score at column 10·j+3 row 19, pause 10000, the right score on row 21.
SierraCh then prints "Press any key for *name*'s corner" (or "... the next
round.") on row 24.

---

## 10. Game flow ✅

```
Title screen (PIC)  ──1 Gym──▶  Gym overlay (PI2): make / view / change / eliminate boxers (60 max)
       │            ──2 Fight─▶ Fight-setup overlay: pick left and right boxers,
       │                         tale of the tape (PI4-style text), odds, schedule
       │                         (first time only, Intro on: ring-girl intro, GRL)
       │                         ──▶ Fight overlay: arena (PI3), rounds, corner between rounds
       │                         ──▶ Judge score cards, end-of-fight statistics (PI4),
       │                              "Another fight? / Rematch? / Quit or Start over?"
       │            ──3 Options─▶ Options overlay (PI5), saved to OV1
       └────────────4 Exit──────▶ "EXIT – are you sure?"
```

The screens around a fight, as in Brett's recording (SierraCh, Arcade, 1
player, left side):

0. **The ring-girl intro** (`FGT 1CAE`). The first time Fight is chosen, and
   only if the Intro option is on, the game loads `GRL` (`GIRL.SPT`) into the
   left boxer's sprite area and draws the empty ring (`PI3`, palette 0 if the
   colours option is 2, else 1). The credits go in the scoreboard panels:
   "Program / by / Dave Murry" at (43,1), (46,2), (42,3) and "Animation / by /
   Barry Murry" at (67,1), (70,2), (66,3); rows 23–24 read "SIERRA
   CHAMPIONSHIP BOXING" (6,23) and "(c) Copyright 1984 Sierra On-Line,Inc."
   (1,24). The girl walks right from screen offset 0C42 to 0C67, 2 bytes a
   frame (head 197C at −3C8, sign 1BC4 at −325, body 0 at p, four leg frames
   1EC/3D4/61C/804 at +410), turns (two frames of 1FBC/1E5C/0/1794, two of
   1EF4/10C/A4C) and walks back left with the "by EVRYWARE" side of the sign
   (1D10, body 10C, legs A4C/CF4/F9C/1244 at +2D0), then turns to face the
   front for the last frames. All pieces are opaque blits (`6B91`) at offsets
   relative to the start of the file. Each frame waits one step of the intro
   tune plus a 500-unit pause, about 225 ms on a 4.77 MHz PC. The tune is a
   table of (divisor, length) pairs ending with 99 stored after "Barry
   Murry\0" in the overlay (`MS2` offset 0x35A, `INTRO.OLY` 0x2BDA); divisor 0
   is a rest and the length counts 500-unit pauses. On the PCjr it plays on
   three voices at attenuations 0, 4 and 8. Any key ends the intro and the
   tune, and the game goes on to AVAILABLE BOXERS.
1. AVAILABLE BOXERS, left then right corner (text mode in SierraCh: grey
   list, the chosen man in red).
2. **Tale of the Tape** (`FGT 06DA`): title at (12,0); the odds line at row 3;
   the names at (6,5) and (26,5); items 1–6 (left) and 7–12 (right) at rows
   7–12: age, weight, reach, height, fights, losses, typed in at column 14 (35
   on the right) and checked ("His age can be between 17 and 50.", 70–350,
   40–99, 60–99, losses no more than fights); 13 rounds (1–15), 14 length,
   15 mode, 16 players and side, 17 new boxers, 18 go outside, 19 begin.
   Edits apply to this fight only.
3. **The odds** (`FGT 17D6`, `1942`): for each man
   `skill + 10·hitter − 4·vulnerable + 3·aggression − age + defence +
   2·(fights − losses) + 2·(reach − 50)`, minus 20 per year over 33 and
   weight/5 over 235 lb; a man more than 20 lb heavier (and under 235) adds
   10 per pound of difference. Both are halved until they are small (both
   under 6, or one under 3 with the other under 11) and one of them is odd;
   2a:a becomes 2:1, and each is at least 1. Sullivan–Corbett gives 472:311,
   which comes down to "SULLIVAN is a 7 to 4 favorite.", as in the recording.
4. In Arcade, the player's corner before every round, round 1 included:
   "*name*'s CORNER", then 'Press "O" for arcade mode options' and "or ... any
   other key to continue." (rows 11 and 13, column 3).
5. The ring and the round (above), then the statistics, then the corner again.
6. In **Simulation** mode (Tale of the Tape item 15; Brett's second
   recording) nobody has a corner: the statistics end with "Press any key for
   the next round." The ring's palette changes every round when the colours
   option is 1 (round 1 cyan/magenta, round 2 green/red/yellow, and so on).
7. **The end of the fight** (`FG2 0C04..12F3`). Rows 22 and 24 are cleared.
   A stoppage keeps "REFEREE STOPS FIGHT DUE TO INJURY!" and the reason on
   rows 22–23. A decision first shows "  Judges are adding their score cards  "
   at (0,22) for pauses of 60000 and 40000 units. Then "WINNER BY A
   KNOCKOUT: name" (or DECISION) at (5,22), or "THIS FIGHT IS A DRAW!" at
   (8,22), and "Press any key for fight statistics." at (2,24). The loser is
   drawn standing (after a decision or stoppage) or flat out (after a
   knock-out), the winner standing (`1165..11E5`). Until a key is pressed,
   `FG2 2690` runs every 4000 pause units: with chance 20/128 the winner gets
   a new frame, glove raised (108/128) or down (20/128); and a flower is
   thrown from the crowd (chance 30/128 to start) at screen offset 105Eh plus
   0–14 bytes, drawn in three frames one byte apart, each erasing the last.
8. **End-of-fight statistics** (`OVR 411F`), on the STAT picture: "FIGHT"
   over "ROUND" in the title (text (56,0)), "WINNER: name" or "DRAW" at
   (11,2), "ROUNDS: n" at (11,3); each man's name and ":" at (0,5)/(0,10), his
   injury at column 10 ("Knocked Out." for the loser of a knock-out) and on
   the next row "Knocked down n time(s)." or "Was not knocked down.";
   "JUDGE SCORE CARDS" at (11,15) and each judge's totals at column 10j+3 on
   rows 19 and 21. Then " Another Fight? (Y or N) _" at (0,24): Y asks
   "Rematch? (Y or N) _" (8,24), N asks "Quit or Start over? (Q or S) _"
   (5,24); Q asks "QUIT - are you sure? (Y or N) _", S goes back to the
   title. Other keys beep. The original then writes the records back if
   their update flag is set (`OVR 368B`, `36C8`); the remake does not.

After a fight, if a boxer's "update record automatically" flag is set, his
fights, losses and KO counts are written back to `BOXING.DAT` (`OVR 36C8` and
`3D6F` write sectors). That is why Brett's `DAT` is dated 1998.

---

## 11. Boxer animation ✅

The boxers are drawn by `OVR 67A3(sequence, boxer)`, which the fight loop
(`FG2 1FD2`) calls once per boxer per loop. It draws one frame and returns the
pointer to that frame's state byte. The fight loop stores that byte in `0EEC`
(left) or `0EEE` (right) and moves on to the next frame. When it reaches a
`63h` byte, the fight loop chooses the next sequence.

### Where things are

The addresses are for the 1987 conversion (`BOXING.OVR` + `OV4`/`FG2`/`FG3`).
SierraCh's `KO.EXE` + `FIGHT.OLY` is the same program linked differently;
its addresses are in the last column. There, the main program's data segment
starts at `KO.EXE` file offset `72B0h`, and the fight overlay (not the main
program) fills the sprite tables.

| DS address | What | SierraCh |
|---|---|---|
|---|---|
| `205F` / `2061` | Each boxer's position: a CGA even-bank byte offset. They are `0BAAh` and `0BBAh` at the bell (line pair 37; pixel columns 104 and 168). `0E3D` = right − left is the distance the AI uses. | `1D42` / `1D44` |
| `20C8` | The **left** boxer's file, `BOXING.xR`, read whole | `1DAC` (`xBXR0.SPT`) |
| `50EE` | The **right** boxer's file, `BOXING.xB`, read whole | `4C6A` (`xBXR1.SPT`) |
| `80FC` | 23 pointers to head sprites (head + upper body, 6×20 words) | `7B10` |
| `812A` | 17 pointers to arm sprites (masked) | `7B3E` |
| `814C` | 23 pointers to body / legs sprites (opaque) | `7B60` |
| `0E7E`–`0EE8` | 27 sequence slots, 4 bytes each: the left boxer's sequence, then the right's | `1558`–`15C2` |

The three sprite tables are filled when the fight starts by code at
`OVR 3DF0`–`40D0`, which stores constants one at a time
(`lea ax,[X]; push ax; lea si,[Y]; pop ax; mov [si],ax`). Entries point into
the boxer files at `20C8`/`50EE` (so every `xR` file shares one layout, and
every `xB` file another). Entry 0 of each table points at an empty sprite in
the main program's data. Head 9 is a blank 6×20 head, also in the main
program, used to wipe the head when a boxer falls.

The sequence slots are filled by `FG2 023A` in the same way. Each slot is
assigned more than once: the later assignments are variants that depend on the
**Arcade fighting style** in `1D06`/`1D08` (1 Slugger, 2 to 5 Boxer with
faster reflexes). The left boxer's style replaces the *right* boxer's punch
sequences and the other way round 🟡 (presumably to give a quicker human more
time to react). The first assignment is the normal
one. The sequences themselves are in the fight overlay's data, which comes
from `BOXING.FG3`.

### A frame

Five bytes:

| Byte | Meaning |
|---|---|
| 0 | Horizontal move in byte columns, plus 10 (`0Ah` = stay, `0Ch` = 2 columns = 8 pixels to the right) |
| 1 | Head sprite number (0 = leave the head as it is) |
| 2 | Arm sprite number |
| 3 | Body / legs sprite number |
| 4 | State: the high nibble is `10h` standing, `20h`/`30h` busy (defending / punching), `40h` down, `50h` staggering. The low nibble is a sub-state that the hit tests read (`0E6B`/`0E6D`). |

A `63h` in byte 0 ends the sequence.

### Drawing a frame

Each part is placed from the head sprite's two offset words (its header is
width, height, offset 1, offset 2):

1. `pos += byte0 − 10`.
2. **Erase the previous arm rectangle.** The original does not save the
   background. It paints a pattern chosen by the screen address so that it
   matches the ring behind: rope rows (`0D6F`–`0DC0`, `0F4F`–`0FA0`,
   `112F`–`1180`: even lines `FFFF`, odd lines `0000`; the rows below each
   rope the other way round), the ring edge (`12B6`–`1310`: `FFFF`), the canvas
   (from `1310`: `5555`), and the crowd stripes elsewhere (`1111`).
3. **Head** (if its width is not 0) at `pos`: 20 line pairs of 6 words, opaque.
4. **Arms** at `pos + head.offset1 − arm.offset`. The first and last word of
   each line are AND-mask / OR-data pairs; the words between are opaque.
5. **Body and legs** at `pos + head.offset2 − body.offset`, opaque. Before
   drawing, the rows above the new body and the old body's width are erased
   with the canvas, ring edge or stripe pattern.

The body is drawn last, so it covers the arms where they overlap.

### The sequences

The slot names below come from playing each sequence and from the code that
picks it. The step and punch sequences are each about 2 to 4 frames, so a
punch takes a few tenths of a second.

| Slot | Sequence | Chosen by |
|---|---|---|
| `0E7E` | Glove up at the face | Defender against a head punch (`29C9`), AI defence |
| `0E82` | Crouch away | Defender against a head punch (`29C2`), AI defence |
| `0E86` | Crouch, then punch from the crouch | AI attack at close range |
| `0E8A` | Head snaps back (state `30h`) | A glancing head blow (`1944`) |
| `0E8E` | Standing guard, 1 frame | Arcade command 7; idle |
| `0E92` | Standing guard, 3 frames | Defender against a body punch (`29D0`) |
| `0E96` | Rocked by a punch | A punch that lands hard (`1AB3`) |
| `0E9A` | Knocked down, flat | `1A38` |
| `09F3`/`0C5E` (not slots) | Knocked down flat near the ropes | `1A1F`/`1A31`; SierraCh `10C3`/`132E` |
| `0A26`/`0C96` (not slots) | Knocked down onto the seat | `1A4A`/`1A51`; SierraCh `10F6`/`1366` |
| `0E9E` | Flat on the canvas | Count, not getting up (`1C82`) |
| `0EA2` | Sitting up | Count (`1C44`) |
| `0EA6` | Getting up | Beat the count (`1C4E`) |
| `0EAA` | Jab | Arcade command 1, AI |
| `0EAE` | Short crouch | Blocked (`1ABF`), off the ropes (`1FBB`), AI defence |
| `0EB2` / `0EB6` | Body punch (two versions) | Arcade command 4 (at random), AI |
| `0EBA` | Head punch with a step in | Arcade command 2, AI |
| `0EBE` | Uppercut | Arcade command 3 (or a jab, depending on a rating), AI |
| `0EC2` / `0EC6` | Staggering about (state `50h`) | `189C` / `18E0` |
| `0ECA` | One frame of stance | Default |
| `0ECE` | Shuffle forward | Arcade command 8, AI closing in |
| `0ED2` / `0ED6` | One / two steps forward | AI closing in; `0ED6` from far away and at the bell |
| `0EDA` / `0EDE` | One / two steps back | Arcade command 9; `0EDE` to a neutral corner during a count |
| `0EE2` | Duck, 1 frame | Arcade command 5 |
| `0EE6` | Cover up, 1 frame | Arcade command 6 |

### Arcade commands

`FG2 28CE` turns an arcade command (1 to 9) into a sequence, as listed above.
Commands 1 to 3 are head punches and command 4 is a body punch; for those it
also rolls whether the defender reacts (crouch or glove up against a head
punch, standing guard against a body punch). The keys are read as scan codes
in `0E67` (left boxer) and `0E69` (right boxer). The left boxer uses **F1 to
F10** (table `1DA8`: F1 = 9 step back, F2 = 8 forward, F3 = 1, F4 = 2, F5 =
4, F6 = 3, F7 = 7, F8 = 6, F9 and F10 = 5). The right boxer uses the
**numeric keypad**, through two tables (`1D22` and then `1DBE`) so that the
keys can be remapped.

### The remake

`Remake/src/anim.c` is a direct port. At run time it builds a 64K copy of the
data segment from the player's own files (the main program's data, the fight
overlay's data, the two boxer files at `20C8` and `50EE`, or SierraCh's
addresses), reads the sprite
tables and sequence slots from the code exactly as listed above, and draws into
a 16K copy of CGA video memory that starts as `BOXING.PI3`. No table or
sequence is copied into the remake's source.

If a boxer file is missing (`BOXING.WR` in the 1987 conversion, so a White
boxer on the left), another character's file for the same side is used and a
note is shown at the start of the fight. SierraCh has the real art. Mirroring the other side does not work,
because the two sides have different sprite sets and layouts.

---

## 12. Open questions

* **Speed.** The original loop has no timer; a round lasts as long as the PC
  takes for 2295 passes. 12.75 passes a second fits both the clock and an
  estimate of a 4.77 MHz PC (section 7), but nobody has timed a real one.
* **Arcade style.** The corner's **R** key (reflexes, `OVR 59DE`) and the
  sequence variants it selects are not in the remake yet; everyone fights as a
  Slugger (style 1), which is the default.
* **Arcade handicaps.** The corner's **M** key (`OVR 5B92`) gives the
  computer aggression 120 and skill 130 and doubles the human's openness and
  vulnerability. It is not clear whether this also applies from the first bell.
* The special keys read in `FG2 2041` (`1E0B`: make a side defensive, all-out,
  or stop) and a small ringside animation (`0E3B`, `FG2 2108`/`22EF`) are not
  in the remake.
* What `8401` (starter) changes exactly. Rounds before the boxer "warms up"
  start his momentum at 40 (`FG2 07E1`).

---

## 13. Sound ✅

All sound goes through a handful of routines in the main program. Sound
option `818C`: 0 off, 1 the PC speaker, 2 "external", the PCjr/Tandy sound
chip at port C0h (`OVR 625F` tone, `6294` volume, `62B6` noise, `62C8` route
it to the speaker, `62D1` silence). Bell option `819A`: 0 beep, 1 ring.

| Routine | What it does |
|---|---|
| `OVR 6229` | flip the speaker's data bit (port 61h bit 1) |
| `OVR 6232` | timer 2 divisor (1193182 Hz ÷ n) |
| `OVR 624D` / `6256` | speaker on (bits 0 and 1) / off |
| `OVR 62EA` | the pause: n calls of the random routine (halved on a fast PC, `1E1D`) |
| `OVR 6472` | **the ring**: hand-made square wave, below |
| `OVR 383B` | **the beep bell**: divisor 900 (1326 Hz) for a 5000 pause |
| `OVR 385A` | **the PCjr bell**: three voices at 88h, 89h, 8Ah (822, 816, 810 Hz), attenuation 0 to 15 in steps of a 1100 pause |
| `OVR 3784` | **wrong key**: divisor 5000 (239 Hz) for a 5000 pause (every menu) |
| `OVR 650D` | divisor 1000 (1193 Hz) for a 1350 pause: the special keys (`FG2 2093`) |

**The ring** (`6472`): with interrupts off it turns the speaker off for `si`
LOOP iterations and on for `di`, `si = di = 240` (180 on a fast PC). After a
number of cycles taken from the table at `DS:1DFF` (1, 2, 4, 8, 16), `di`
grows by 4 and `si` shrinks by 4; 12 such steps per table entry. The period
stays 480 iterations (about 585 Hz on a 4.77 MHz PC) while the wave turns
into a solid "on", so the bell fades out over 372 cycles, about 0.65 s.

**In the fight** (`FG2`):

* The bell (ring, PCjr bell or beep, by the options) after the introductions
  (`089C`), at the end of a round (`2431`) and on a knock-out (`22A1`).
* A click for every number of the count: divisor 400 (2983 Hz) for a 50 pause
  (`2227`).
* **Punches** (`24EA`, called once per pass at `203E` in place of the
  800 pause): `8399` holds the sound. The timer plays a tone and, for `cx`
  passes of a loop, the data bit is flipped whenever `random() < dx`, which
  chops the tone into noise:

  | `8399` | When | Divisor (Hz) | dx | Passes |
  |---|---|---|---|---|
  | 1 | gloves touch (`2838`, `28B8`) | 300 (3977) | 35 | 150 |
  | 3 | a head punch reaches him (`1833`) | 600 (1989) | 55 | 500 |
  | 2 | a body punch reaches him (`1833`) | 12000 (99) | 5 | 300 |
  | 10 | a frame in state 48h, hitting the canvas (`2031`) | 6000 (199) | 25 | 450 |

* "REFEREE STOPS FIGHT DUE TO INJURY!": divisor 15000 (80 Hz) for a 4000
  pause (`0C5E`).
* After a fight, with the PCjr chip only, a crowd roar: noise whose volume
  swells and fades while the program waits for a key (`FG2 1203`–`12D1`).

The remake (`audio.c`) synthesises these through SDL3 with a speaker model
(the level is averaged at 8× the output rate, then a DC blocker), with the
pause and loop timings of a 4.77 MHz PC. The PCjr crowd roar and the
special-key beep are not in it.

---

## 14. Tools

`tools/re/` contains the scripts used for this work. They only read the
original files and print text:

* `cbdis.py <GYM|FGT|FG2|OPT|OVR>`: builds the in-memory image for that
  overlay (main program plus overlay code and data at the right addresses) and
  disassembles it with Capstone, marking function prologues and string
  references.
* `lift.py <file.asm> names.txt ds_<name>.bin`: turns the compiler's
  stack-machine output into readable pseudo-C (works well for the compiled
  parts, less so for the hand-written fight loop).
* `names.txt`: names given to known routines and variables.

```
pip install capstone
cd <folder with the BOXING.* files>
python3 path/to/tools/re/cbdis.py FG2
python3 path/to/tools/re/lift.py FG2.asm path/to/tools/re/names.txt ds_FG2.bin > FG2.c
```

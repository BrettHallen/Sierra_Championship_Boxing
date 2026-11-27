#include "anim.h"
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where each release keeps things in its data segment (docs/GAMEPLAY.md
 * section 11). The two builds are the same program linked differently. */
struct Layout {
    unsigned main_ds;   /* file offset of DS:0000 in the main program */
    unsigned heads;     /* 23 words: head + upper body sprites */
    unsigned arms;      /* 17 words: masked arm sprites */
    unsigned bodies;    /* 23 words: legs / whole-body sprites */
    unsigned left;      /* the left boxer's file (xR) */
    unsigned right;     /* the right boxer's file (xB) */
    unsigned slots;     /* sequence slot table */
    unsigned tab_lo, tab_hi; /* code range (in MAIN, 0 = in FIGHT) that fills the tables */
};

static const Layout layout_1987 = {     /* BOXING.OVR + OV4/FG2/FG3 */
    0x200 + 0x75A0, 0x80FC, 0x812A, 0x814C, 0x20C8, 0x50EE, 0x0E7E, 0x3DF0, 0x40D0
};
static const Layout layout_sierrach = { /* KO.EXE + FIGHT.OLY */
    0x200 + 0x70B0, 0x7B10, 0x7B3E, 0x7B60, 0x1DAC, 0x4C6A, 0x1558, 0, 0
};
#define L (a->lay)
#define DS_HEADS  (L->heads)
#define DS_ARMS   (L->arms)
#define DS_BODIES (L->bodies)
#define DS_LEFT   (L->left)
#define DS_RIGHT  (L->right)
#define DS_SEQ    (L->slots)
#define SEQ_END    0x63

static unsigned rd(const Anim *a, unsigned addr)
{
    return a->ds[addr & 0xFFFF] | a->ds[(addr + 1) & 0xFFFF] << 8;
}
static int rds(const Anim *a, unsigned addr) { return (int16_t)rd(a, addr); }

static void ww(Anim *a, int addr, unsigned w)
{
    a->vram[addr & 0x3FFF] = w & 0xFF;
    a->vram[(addr + 1) & 0x3FFF] = (w >> 8) & 0xFF;
}
static unsigned rw(const Anim *a, int addr)
{
    return a->vram[addr & 0x3FFF] | a->vram[(addr + 1) & 0x3FFF] << 8;
}

/* Scan code for "lea ax,[X]; push ax; lea si,[Y]; pop ax; mov [si],ax", the
 * compiler's way of storing a pointer constant: calls fn(Y, X). */
static int scan_stores(const uint8_t *c, size_t len, Anim *a, int first_only, uint16_t *seen, int nseen,
                       unsigned lo, unsigned hi)
{
    int n = 0;
    for (size_t i = 0; i + 12 <= len; i++) {
        if (c[i] != 0x8D || c[i + 1] != 0x06 || c[i + 4] != 0x50 || c[i + 5] != 0x8D ||
            c[i + 6] != 0x36 || c[i + 9] != 0x58 || c[i + 10] != 0x89 || c[i + 11] != 0x04) continue;
        unsigned x = c[i + 2] | c[i + 3] << 8, y = c[i + 7] | c[i + 8] << 8;
        if (first_only) {
            /* sequence table: later stores to a slot are injury variants */
            if (y < DS_SEQ || y >= DS_SEQ + 4 * SEQ_SLOTS || (y & 1)) continue;
            int k = (y - DS_SEQ) / 2;
            if (k >= nseen || seen[k]) continue;
            seen[k] = 1;
            a->seq[k / 2][k & 1] = (uint16_t)x;
        } else {
            if (y < lo || y >= hi) continue;
            a->ds[y] = x & 0xFF; a->ds[(y + 1) & 0xFFFF] = x >> 8;
        }
        n++;
        i += 11;
    }
    return n;
}

/* The two falls FG2 loads with "lea dx,[X]" in its knock-down code. They are
 * found by the instructions around them, which both releases share:
 *   cmp bp,0BA0h / jge / lea dx,[left far]     cmp bp,0BC5h / jle / lea dx,[right far]
 *   cmp si,0 / jne / lea dx,[left sit] / jmp / nop / lea dx,[right sit] */
static void find_falls(const uint8_t *c, size_t len, Anim *a)
{
    for (size_t i = 0; i + 16 <= len; i++) {
        if (c[i] == 0x81 && c[i + 1] == 0xFD && c[i + 2] == 0xA0 && c[i + 3] == 0x0B && c[i + 4] == 0x7D &&
            c[i + 6] == 0x8D && c[i + 7] == 0x16)
            a->seq[SEQ_FALL_ROPES][0] = c[i + 8] | c[i + 9] << 8;
        if (c[i] == 0x81 && c[i + 1] == 0xFD && c[i + 2] == 0xC5 && c[i + 3] == 0x0B && c[i + 4] == 0x7E &&
            c[i + 6] == 0x8D && c[i + 7] == 0x16)
            a->seq[SEQ_FALL_ROPES][1] = c[i + 8] | c[i + 9] << 8;
        if (c[i] == 0x83 && c[i + 1] == 0xFE && c[i + 2] == 0x00 && c[i + 3] == 0x75 &&
            c[i + 5] == 0x8D && c[i + 6] == 0x16 && c[i + 9] == 0xEB && c[i + 11] == 0x90 &&
            c[i + 12] == 0x8D && c[i + 13] == 0x16 && !a->seq[SEQ_FALL_SIT][0]) {
            a->seq[SEQ_FALL_SIT][0] = c[i + 7] | c[i + 8] << 8;
            a->seq[SEQ_FALL_SIT][1] = c[i + 14] | c[i + 15] << 8;
        }
    }
}

/* FG2 2573: "lea bx,[sprite]; mov cx,offset" for each of the clock's 17
 * steps; the screen offsets are the same in both releases. */
static void find_clock(const uint8_t *c, size_t len, Anim *a)
{
    static const unsigned offs[] = {0x2F6, 0x396, 0x436, 0x526, 0x576, 0x574, 0x522, 0x432, 0x392, 0x344};
    a->nclock = 0;
    for (size_t i = 0; i + 7 <= len && a->nclock < 17; i++) {
        if (c[i] != 0x8D || c[i + 1] != 0x1E || c[i + 4] != 0xB9) continue;
        unsigned cx = c[i + 5] | c[i + 6] << 8;
        for (size_t k = 0; k < sizeof offs / sizeof offs[0]; k++)
            if (cx == offs[k]) {
                a->clock[a->nclock][0] = c[i + 2] | c[i + 3] << 8;
                a->clock[a->nclock][1] = (uint16_t)cx;
                a->nclock++;
                break;
            }
    }
}

/* FG2 27E2 (round 1) starts "mov [pos],0BAAh; mov [pos],0BBAh", then draws
 * four frames with "lea ax,[frame]": standing (left, right), then touching
 * gloves (left, right). */
static void find_touch(const uint8_t *c, size_t len, Anim *a)
{
    for (size_t i = 0; i + 0x60 <= len; i++) {
        if (c[i] != 0xC7 || c[i + 1] != 0x06 || c[i + 4] != 0xAA || c[i + 5] != 0x0B ||
            c[i + 6] != 0xC7 || c[i + 7] != 0x06 || c[i + 10] != 0xBA || c[i + 11] != 0x0B) continue;
        unsigned found[4], n = 0;
        for (size_t k = i + 12; k + 4 <= i + 0x60 && n < 4; k++)
            if (c[k] == 0x8D && c[k + 1] == 0x06) { found[n++] = c[k + 2] | c[k + 3] << 8; k += 3; }
        if (n == 4) { a->touch[0] = (uint16_t)found[2]; a->touch[1] = (uint16_t)found[3]; }
        return;
    }
}

/* FG2 1165: after the fight the loser is drawn with "lea ax,[frame]" for
 * the left boxer then the right, first after a decision or stoppage, then
 * after a knock-out (each block 2Eh bytes). FG2 2690: the winner's frames are
 * four "lea dx,[frame]" after "cmp bx,3 / je / cmp bx,0 / jne"; the flower
 * follows: "sub ax,51h ... lea ax,[patch]", two "lea bx,[frame]", and
 * "add ax,105Eh ... lea ax,[frame]". */
static void find_ending(const uint8_t *c, size_t len, Anim *a)
{
    int blocks = 0;
    size_t first = 0;
    for (size_t i = 0; i + 0x80 <= len; i++) {
        if (blocks < 2 && c[i] == 0x33 && c[i + 1] == 0xC0 && c[i + 2] == 0x50 && c[i + 3] == 0x8D &&
            c[i + 4] == 0x06 && c[i + 7] == 0x50 && c[i + 8] == 0xE8 && c[i + 11] == 0x83 &&
            c[i + 12] == 0xC4 && c[i + 13] == 0x04 && c[i + 14] == 0xE9 && c[i + 17] == 0xB8 &&
            c[i + 18] == 0x01 && c[i + 19] == 0x00 && c[i + 20] == 0x50 && c[i + 21] == 0x8D &&
            c[i + 22] == 0x06) {
            if (blocks == 1 && i != first + 0x2E) blocks = 0;   /* not the pair: start again */
            if (blocks == 0) first = i;
            a->end_pose[blocks][0] = c[i + 5] | c[i + 6] << 8;
            a->end_pose[blocks][1] = c[i + 23] | c[i + 24] << 8;
            blocks++;
        }
        if (c[i] == 0x83 && c[i + 1] == 0xFB && c[i + 2] == 0x03 && c[i + 3] == 0x74 &&
            c[i + 5] == 0x83 && c[i + 6] == 0xFB && c[i + 7] == 0x00 && c[i + 8] == 0x75) {
            unsigned f[4], n = 0;
            size_t k;
            for (k = i + 9; k + 4 <= i + 0x40 && n < 4; k++)
                if (c[k] == 0x8D && c[k + 1] == 0x16) { f[n++] = c[k + 2] | c[k + 3] << 8; k += 3; }
            if (n < 4) continue;
            a->cheer[0][0] = f[0]; a->cheer[0][1] = f[1];
            a->cheer[1][0] = f[2]; a->cheer[1][1] = f[3];
            unsigned g[4] = {0, 0, 0, 0};
            for (; k + 8 <= i + 0xC0 && k + 8 <= len; k++) {
                if (c[k] == 0x2D && c[k + 1] == 0x51 && c[k + 2] == 0x00 && c[k + 3] == 0x50 &&
                    c[k + 4] == 0x8D && c[k + 5] == 0x06 && !g[3]) g[3] = c[k + 6] | c[k + 7] << 8;
                else if (c[k] == 0x8D && c[k + 1] == 0x1E) {
                    if (!g[1]) g[1] = c[k + 2] | c[k + 3] << 8;
                    else if (!g[2]) g[2] = c[k + 2] | c[k + 3] << 8;
                } else if (c[k] == 0x05 && c[k + 1] == 0x5E && c[k + 2] == 0x10) {
                    for (size_t m = k; m + 4 <= k + 16; m++)
                        if (c[m] == 0x8D && c[m + 1] == 0x06) { g[0] = c[m + 2] | c[m + 3] << 8; break; }
                    break;
                }
            }
            if (g[0] && g[1] && g[2] && g[3]) memcpy(a->flower, (uint16_t[4]){g[0], g[1], g[2], g[3]}, sizeof a->flower);
            break;
        }
    }
}

static void fb_to_vram(const Framebuffer *fb, uint8_t *v)
{
    memset(v, 0, 0x4000);
    for (int y = 0; y < CGA_H; y++)
        for (int x = 0; x < CGA_W; x++) {
            int addr = (y & 1) * 0x2000 + (y >> 1) * 80 + x / 4;
            v[addr] |= (fb->px[y * CGA_W + x] & 3) << (6 - 2 * (x & 3));
        }
}

void anim_to_fb(const Anim *a, Framebuffer *fb)
{
    for (int y = 0; y < CGA_H; y++)
        for (int x = 0; x < CGA_W; x++) {
            int addr = (y & 1) * 0x2000 + (y >> 1) * 80 + x / 4;
            fb->px[y * CGA_W + x] = (a->vram[addr] >> (6 - 2 * (x & 3))) & 3;
        }
}

int anim_load(Anim *a, const char *dir)
{
    memset(a, 0, sizeof *a);
    a->ds = calloc(1, 0x10000);
    if (!a->ds) return 0;
    a->lay = assets_layout(dir) == LAYOUT_SIERRACH ? &layout_sierrach : &layout_1987;
    size_t lm = 0, lx = 0, lp = 0;
    uint8_t *main_prg = assets_read(dir, "MAIN", &lm);
    uint8_t *x = assets_read(dir, "FIGHT", &lx);
    uint8_t *pi3 = assets_read(dir, "PI3", &lp);
    int tables = 0, seqs = 0;

    if (main_prg && lm > L->main_ds) {
        size_t n = lm - L->main_ds;
        if (n > 0x10000) n = 0x10000;
        memcpy(a->ds, main_prg + L->main_ds, n);
    }
    if (x && lx >= 16) {
        /* overlay header: code size at 2, data size at 6; code loads at
         * CS:0100, data at DS:0002 */
        size_t cz = x[2] | x[3] << 8, dz = x[6] | x[7] << 8;
        if (16 + cz + dz <= lx && dz + 2 <= 0x10000) {
            memcpy(a->ds + 2, x + 16 + cz, dz);
            uint16_t seen[SEQ_SLOTS * 2] = {0};
            /* FG2 023A fills the slot table; later stores are injury variants */
            seqs = scan_stores(x + 16, cz, a, 1, seen, SEQ_SLOTS * 2, 0, 0);
            find_falls(x + 16, cz, a);
            find_clock(x + 16, cz, a);
            find_touch(x + 16, cz, a);
            find_ending(x + 16, cz, a);
            if (!L->tab_lo)   /* SierraCh: the fight overlay fills the sprite tables */
                tables = scan_stores(x + 16, cz, a, 0, NULL, 0, L->heads, L->heads + 0x98);
        }
    }
    if (L->tab_lo && main_prg && lm > 0x200 + L->tab_hi - 0x100) {
        /* 1987: OVR 3DF0..40D0 fills the sprite pointer tables */
        tables = scan_stores(main_prg + 0x200 + (L->tab_lo - 0x100), L->tab_hi - L->tab_lo, a, 0, NULL, 0,
                             L->heads, L->heads + 0x98);
    }
    if (pi3) {
        Framebuffer *fb = malloc(sizeof *fb);
        if (fb) { cga_decode_screen(pi3, lp, fb); fb_to_vram(fb, a->bg); free(fb); }
    }
    free(main_prg); free(x); free(pi3);
    a->ok = tables >= 40 && seqs == SEQ_SLOTS * 2;
    for (int k = SEQ_SLOTS; k < SEQ_COUNT; k++)
        for (int sd = 0; sd < 2; sd++)
            if (!a->seq[k][sd]) a->seq[k][sd] = a->seq[SEQ_FALL][sd];
    a->b[0].seq = a->b[1].seq = -1;
    return a->ok;
}

void anim_free(Anim *a) { free(a->ds); a->ds = NULL; a->ok = 0; }

static int place(Anim *a, const char *dir, int ch, int side, char *used)
{
    static const char chars[] = "BWCK";
    unsigned at = side ? DS_RIGHT : DS_LEFT;
    for (int t = 0; t <= CHAR_COUNT; t++) {
        int c = t == 0 ? ch : t - 1;
        if (t > 0 && c == ch) continue;
        char ext[3] = {chars[c], side ? 'B' : 'R', 0};
        size_t len = 0;
        uint8_t *d = assets_read(dir, ext, &len);
        if (!d) continue;
        if (len > 0x10000 - at) len = 0x10000 - at;
        memcpy(a->ds + at, d, len);
        free(d);
        *used = chars[c];
        return 1;
    }
    return 0;
}

int anim_set_boxers(Anim *a, const char *dir, int left_char, int right_char)
{
    static const char chars[] = "BWCK";
    if (!a->ok) return 0;
    if (left_char < 0 || left_char >= CHAR_COUNT) left_char = 0;
    if (right_char < 0 || right_char >= CHAR_COUNT) right_char = 0;
    char ul = 0, ur = 0;
    int okl = place(a, dir, left_char, 0, &ul);
    int okr = place(a, dir, right_char, 1, &ur);
    static const char *names[] = {"Black", "White", "Cartoon", "Kangaroo"};
    a->note[0] = 0;
    if (okl && ul != chars[left_char])
        snprintf(a->note, sizeof a->note, "No %s left-side art: using %s", names[left_char],
                 names[strchr(chars, ul) - chars]);
    else if (okr && ur != chars[right_char])
        snprintf(a->note, sizeof a->note, "No %s right-side art: using %s", names[right_char],
                 names[strchr(chars, ur) - chars]);
    a->have_boxers = okl && okr;
    return a->have_boxers;
}

void anim_reset_at(Anim *a, int left_pos, int right_pos)
{
    memcpy(a->vram, a->bg, sizeof a->vram);
    for (int s = 0; s < 2; s++) {
        AnimBoxer *b = &a->b[s];
        memset(b, 0, sizeof *b);
        b->pos = s ? right_pos : left_pos;
        b->seq = -1;
    }
}

void anim_reset(Anim *a) { anim_reset_at(a, ANIM_START_LEFT, ANIM_START_RIGHT); }

void anim_play(Anim *a, int side, int seq)
{
    if (!a->ok || seq < 0 || seq >= SEQ_COUNT) return;
    a->b[side].seq = seq;
    a->b[side].p = a->seq[seq][side];
}

int anim_busy(const Anim *a, int side)
{
    const AnimBoxer *b = &a->b[side];
    return a->ok && b->seq >= 0 && b->p && a->ds[b->p & 0xFFFF] != SEQ_END;
}

void anim_blit(Anim *a, unsigned addr, int off)
{
    if (!a->ds) return;
    int w = rd(a, addr), h = rd(a, addr + 2);
    unsigned q = addr + 6;
    for (int row = 0; row < h; row++, off += 0x50)
        for (int k = 0; k < w; k++, q += 4) {
            ww(a, off + 2 * k, rd(a, q));
            ww(a, off + 2 * k + 0x2000, rd(a, q + 2));
        }
}

int anim_load_girl(Anim *a, const char *dir)
{
    size_t len = 0;
    uint8_t *d = a->ds ? assets_read(dir, "GRL", &len) : NULL;
    if (!d) return 0;
    if (len > 0x10000 - DS_LEFT) len = 0x10000 - DS_LEFT;
    memcpy(a->ds + DS_LEFT, d, len);
    free(d);
    a->have_boxers = 0;   /* the left boxer's art is gone until anim_set_boxers */
    memcpy(a->vram, a->bg, sizeof a->vram);
    return 1;
}

void anim_girl(Anim *a, unsigned file_off, int screen_off) { anim_blit(a, DS_LEFT + file_off, screen_off); }

void anim_cheer(Anim *a, int winner, int (*rnd)(void *), void *ctx)
{
    if (!a->ok) return;
    if (rnd(ctx) < 0x14 && winner >= 0 && winner <= 1) {
        unsigned fr = a->cheer[winner][rnd(ctx) < 0x14 ? 0 : 1];
        if (fr && a->have_boxers) anim_frame_at(a, winner, fr);
    }
    if (!a->flower[0]) return;
    if (a->flower_step) {
        int step = ++a->flower_step;
        anim_blit(a, a->flower[3], a->flower_pos - 0x51);
        if (step == 2) anim_blit(a, a->flower[1], a->flower_pos + 1);
        else if (step == 3) anim_blit(a, a->flower[2], a->flower_pos + 2);
        else a->flower_step = 0;
    } else if (rnd(ctx) < 0x1E) {
        a->flower_step = 1;
        a->flower_pos = 0x105E + rnd(ctx) / 9;
        anim_blit(a, a->flower[0], a->flower_pos - 1);
    }
}

void anim_cheer_end(Anim *a)
{
    if (a->flower_step && a->flower[3]) anim_blit(a, a->flower[3], a->flower_pos - 0x51);
    a->flower_step = 0;
}

void anim_clock(Anim *a, int step)
{
    if (a->ok && a->nclock == 17 && step >= 1 && step <= 17)
        anim_blit(a, a->clock[step - 1][0], a->clock[step - 1][1]);
}

int anim_ended(const Anim *a, int side)
{
    const AnimBoxer *b = &a->b[side];
    return !a->ok || b->seq < 0 || !b->p || a->ds[b->p & 0xFFFF] == SEQ_END;
}

int anim_x(const Anim *a, int side) { return (a->b[side].pos % 80) * 4; }

/* Erase patterns: the original paints the vacated area with a pattern that
 * matches the arena behind it (ropes, canvas, apron) rather than saving the
 * background. */
static void arm_pattern(int di, unsigned *even, unsigned *odd)
{
    if ((di > 0xD6F && di < 0xDC0) || (di > 0xF4F && di < 0xFA0) || (di > 0x112F && di < 0x1180)) { *even = 0xFFFF; *odd = 0; return; }
    if ((di > 0xDBF && di < 0xE10) || (di > 0xF9F && di < 0xFF0) || (di > 0x117F && di < 0x11D0)) { *even = 0; *odd = 0xFFFF; return; }
    if (di > 0x12B6 && di < 0x1310) { *even = *odd = 0xFFFF; return; }
    if (di > 0x130F) { *even = *odd = 0x5555; return; }
    *even = *odd = 0x1111;
}
static unsigned body_pattern(int di)
{
    if (di >= 0x1310) return 0x5555;
    if (di > 0x12BF) return 0xFFFF;
    return 0x1111;
}

/* One frame: OVR 67A3. A frame is 5 bytes: column move + 10, head, arm and
 * body sprite numbers, state byte. */
static void draw_frame(Anim *a, int side, unsigned p)
{
    AnimBoxer *b = &a->b[side];
    b->pos += a->ds[p & 0xFFFF] - 10;
    b->state = a->ds[(p + 4) & 0xFFFF];
    if (!a->have_boxers) return;   /* the fight logic still needs pos and state */
    int pos = b->pos;
    unsigned H = rd(a, DS_HEADS + 2 * a->ds[(p + 1) & 0xFFFF]);
    unsigned A = rd(a, DS_ARMS + 2 * a->ds[(p + 2) & 0xFFFF]);
    unsigned B = rd(a, DS_BODIES + 2 * a->ds[(p + 3) & 0xFFFF]);

    /* arms: placed from the head's first offset; erase the old rectangle */
    int aw = rd(a, A), ah = rd(a, A + 2), armpos = pos + rds(a, H + 4) - rds(a, A + 4);
    int anchor = pos + rds(a, H + 6);
    for (int row = 0, di = b->arm_pos; row < b->arm_h; row++, di += 0x50) {
        unsigned e, o;
        arm_pattern(di, &e, &o);
        for (int k = 0; k < b->arm_w; k++) { ww(a, di + 2 * k, e); ww(a, di + 2 * k + 0x2000, o); }
    }
    b->arm_pos = armpos; b->arm_w = aw; b->arm_h = ah;

    /* head + upper body: 20 line pairs of 6 words, opaque (width 0 = keep) */
    if (rd(a, H)) {
        unsigned q = H + 8;
        for (int row = 0; row < 20; row++)
            for (int k = 0; k < 6; k++, q += 4) {
                ww(a, pos + row * 80 + 2 * k, rd(a, q));
                ww(a, pos + row * 80 + 2 * k + 0x2000, rd(a, q + 2));
            }
    }

    /* arms: edge words are AND/OR masked, the middle is opaque */
    unsigned q = A + 6;
    for (int row = 0; row < ah; row++) {
        int di = armpos + row * 80;
        for (int bank = 0; bank <= 0x2000; bank += 0x2000, q += 4)
            ww(a, di + bank, (rw(a, di + bank) & rd(a, q)) | rd(a, q + 2));
        for (int k = 1; k < aw - 1; k++, q += 4) {
            ww(a, di + 2 * k, rd(a, q)); ww(a, di + 2 * k + 0x2000, rd(a, q + 2));
        }
        if (aw > 1)
            for (int bank = 0; bank <= 0x2000; bank += 0x2000, q += 4) {
                int ad = di + 2 * (aw - 1) + bank;
                ww(a, ad, (rd(a, q) & rw(a, ad)) | rd(a, q + 2));
            }
    }

    /* body / legs: placed from the head's second offset, opaque; the rows
     * above the new body and the old width are erased first */
    int bw = rd(a, B), bh = rd(a, B + 2), bp = anchor - rds(a, B + 4);
    q = B + 6;
    int di = b->body_pos, ow = b->body_w, ax = di;
    b->body_pos = bp; b->body_w = bw;
    if (ow == 0) ow = 1;
    else
        while (di < bp) {
            unsigned pp = body_pattern(di);
            for (int k = 0; k < ow; k++) { ww(a, di + 2 * k, pp); ww(a, di + 2 * k + 0x2000, pp); }
            ax += 0x50; di = ax;
        }
    for (int row = 0; row < bh; row++) {
        if (di < bp + 0x50) {
            unsigned pp = body_pattern(di);
            for (int k = 0; k < ow; k++) { ww(a, di + 2 * k, pp); ww(a, di + 2 * k + 0x2000, pp); }
            ax += 0x50; di = ax;
        }
        for (int k = 0; k < bw; k++, q += 4) {
            ww(a, bp + 2 * k, rd(a, q)); ww(a, bp + 2 * k + 0x2000, rd(a, q + 2));
        }
        bp += 0x50;
    }
}

int anim_frame(Anim *a, int side)
{
    AnimBoxer *b = &a->b[side];
    if (!a->ok) return 0;
    if (anim_ended(a, side)) anim_play(a, side, SEQ_IDLE);
    draw_frame(a, side, b->p);
    b->p += 5;
    return b->state;
}

void anim_frame_at(Anim *a, int side, unsigned addr)
{
    AnimBoxer *b = &a->b[side];
    if (!a->ok || !addr) return;
    draw_frame(a, side, addr);
    b->seq = -1;          /* nothing playing: the caller decides what is next */
    b->p = addr + 5;
}

void anim_step(Anim *a, int side)
{
    if (!a->ok || !a->have_boxers) return;
    anim_frame(a, side);
}

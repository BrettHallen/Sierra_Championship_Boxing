#include "screens.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WHITE 3
#define CYAN 1
#define MAGENTA 2

/* End-of-round statistics (FG2 1462): each judge's card is turned over after
 * pauses of 25000 and 10000 calls, about 1.1 s and 0.45 s. */
#define STATS_STEP 16   /* ticks per judge: 11 before the left score, 5 before the right */
#define STATS_DONE (3 * STATS_STEP)

/* ------------------------------------------------------------- text layer */

void txt_clear(App *app)
{
    for (int r = 0; r < TEXT_ROWS; r++) {
        memset(app->text[r], ' ', TEXT_COLS);
        app->text[r][TEXT_COLS] = 0;
        memset(app->text_colour[r], 0xFF, TEXT_COLS);
    }
    app->nptext = 0;
}

void ptxt_at(App *app, int x, int y, uint8_t colour, const char *fmt, ...)
{
    if (app->nptext >= (int)(sizeof app->ptext / sizeof app->ptext[0])) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->ptext[app->nptext].s, sizeof app->ptext[0].s, fmt, ap);
    va_end(ap);
    app->ptext[app->nptext].x = x;
    app->ptext[app->nptext].y = y;
    app->ptext[app->nptext].colour = colour;
    app->nptext++;
}

void app_sfx(App *app, int sfx)
{
    if (app->nsfx < (int)(sizeof app->sfx / sizeof app->sfx[0])) app->sfx[app->nsfx++] = sfx;
}

/* The SierraCh release draws its menus in text mode: grey text, bright
 * headings, the chosen boxer in red. The 1987 release uses CGA graphics. */
static int text_mode(const App *app) { return app->assets.layout == LAYOUT_SIERRACH; }
static uint8_t c_text(const App *app) { return text_mode(app) ? TXT_GREY : 3; }
static uint8_t c_head(const App *app) { return text_mode(app) ? TXT_BRIGHT : 2; }

void txt_at(App *app, int col, int row, uint8_t colour, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (row < 0 || row >= TEXT_ROWS) return;
    for (int i = 0; buf[i] && col + i < TEXT_COLS; i++) {
        if (col + i < 0) continue;
        app->text[row][col + i] = buf[i];
        app->text_colour[row][col + i] = colour;
    }
}

static void show_screen(App *app, int which)
{
    if (app->assets.screens[which].loaded) app->fb = app->assets.screens[which].fb;
    else memset(app->fb.px, 0, sizeof app->fb.px);
}

static void clear_fb(App *app) { memset(app->fb.px, 0, sizeof app->fb.px); }

/* ------------------------------------------------------------ boxer drawing */

/* The fight logic plays the original's own sequences (anim.c) itself, as the
 * original did; this just shows the first picture of a round. */
static void start_round_anim(App *app)
{
    if (app->anim.note[0] && app->fight.round == 1)
        snprintf(app->fight.line2, sizeof app->fight.line2, "%.39s", app->anim.note);
    /* draw both boxers so the first picture is complete */
    if (app->anim.ok)
        for (int i = 0; i < 2; i++) {
            AnimBoxer save = app->anim.b[i];
            anim_frame(&app->anim, i);
            app->anim.b[i].pos = save.pos; app->anim.b[i].p = save.p; app->anim.b[i].state = save.state;
        }
}

/* --------------------------------------------------------------- states */

static int fight_palette(const App *app)
{
    int c = app->assets.options.colours;
    if (c == 2) return 0;
    if (c == 1) return (app->fight.round & 1) ? 1 : 0;
    return 1;
}

void app_set_state(App *app, State s)
{
    app->state = s;
    app->timer = 0;
    app->input[0] = 0;
    app->status[0] = 0;
}

/* The intro tune (FGT 1CAE / 231F): pairs of timer divisor (0 = rest) and
 * length, ending with 99. It sits after the credits in BOXING.MS2 (1987) or
 * INTRO.OLY (SierraCh); we find it by its end, "0, 200, 99", and walk back
 * over pairs that look like notes. */
static void find_tune(App *app, const char *dir)
{
    size_t len = 0;
    uint8_t *d = assets_read(dir, "MS2", &len);
    app->ntune = 0;
    if (!d) return;
    for (size_t e = 0; e + 6 <= len; e += 1) {
        if (!(d[e] == 0 && d[e + 1] == 0 && d[e + 2] == 200 && d[e + 3] == 0 && d[e + 4] == 99 && d[e + 5] == 0)) continue;
        size_t b = e;
        while (b >= 4) {
            unsigned dv = d[b - 4] | d[b - 3] << 8, ln = d[b - 2] | d[b - 1] << 8;
            if (!((dv == 0 || (dv >= 500 && dv <= 20000)) && ln >= 1 && ln <= 255)) break;
            b -= 4;
        }
        if ((e - b) / 4 < 8) continue;
        for (size_t q = b; q <= e && app->ntune < 128; q += 4) {
            app->tune[app->ntune][0] = (uint16_t)(d[q] | d[q + 1] << 8);
            app->tune[app->ntune][1] = (uint16_t)(d[q + 2] | d[q + 3] << 8);
            app->ntune++;
        }
        break;
    }
    free(d);
}

void app_init(App *app, const char *dir)
{
    memset(app, 0, sizeof *app);
    app->palette = 1;
    app->pick[0] = app->pick[1] = -1;
    char found[1024];
    if (!assets_find_dir(dir, found, sizeof found) || !assets_load(&app->assets, found)) {
        app_set_state(app, ST_ERROR);
        return;
    }
    /* Random source: the user's own fight overlay code, like the original. */
    size_t lx = 0;
    uint8_t *x = assets_read(found, "FIGHT", &lx);
    if (x && lx > 16) {
        size_t cz = x[2] | x[3] << 8;
        if (cz > lx - 16) cz = lx - 16;
        app->rng_len = cz;
        app->rng_code = malloc(cz);
        if (app->rng_code) memcpy(app->rng_code, x + 16, cz);
    }
    free(x);
    snprintf(app->game_dir, sizeof app->game_dir, "%s", found);
    anim_load(&app->anim, found);
    find_tune(app, found);
    app_set_state(app, ST_TITLE);
}

void app_free(App *app)
{
    assets_free(&app->assets);
    anim_free(&app->anim);
    free(app->rng_code);
}

/* ------------------------------------------------------------ the ring girl */

/* FGT 1CAE..22CB. She walks from offset 0C42h to 0C67h two bytes (8 pixels)
 * a frame, turns, walks back and turns again. Each frame is four opaque
 * pieces of GIRL.SPT: the sign, head and arms, body, legs (four leg frames
 * each way). A frame lasts ten steps of the tune with a 500-call pause each
 * (FGT 22CB): about 225 ms on a 4.77 MHz PC. */
#define GIRL_FRAME_MS 225
static void girl_draw(App *app, const unsigned (*pc)[2], int n)
{
    for (int i = 0; i < n; i++) anim_girl(&app->anim, pc[i][0], app->girl_pos + (int)pc[i][1]);
}

static void girl_tick(App *app)
{
    static const unsigned legs_r[4] = {0x1EC, 0x3D4, 0x61C, 0x804};
    static const unsigned legs_l[4] = {0xA4C, 0xCF4, 0xF9C, 0x1244};
    while (app->girl_wait_ms <= 0 && app->state == ST_GIRL) {
        int frames = 1;
        switch (app->girl_stage) {
        case 0: {   /* walking right (1EC0) */
            app->girl_phase = app->girl_phase % 4 + 1;
            const unsigned pc[4][2] = {{0x197C, (unsigned)-0x3C8}, {0x1BC4, (unsigned)-0x325}, {0, 0},
                                       {legs_r[app->girl_phase - 1], 0x410}};
            girl_draw(app, pc, 4);
            if (app->girl_pos < 0x0C67) app->girl_pos += 2; else app->girl_stage = 1;
            break;
        }
        case 1: {   /* turning (1FD8) */
            const unsigned pc[4][2] = {{0x1FBC, (unsigned)-0x3C8}, {0x1E5C, (unsigned)-0x3C0}, {0, 0}, {0x1794, 0x412}};
            girl_draw(app, pc, 4); frames = 2; app->girl_stage = 2;
            break;
        }
        case 2: {
            const unsigned pc[3][2] = {{0x1EF4, (unsigned)-0x3C0}, {0x10C, 0}, {0xA4C, 0x2D0}};
            girl_draw(app, pc, 3); frames = 2; app->girl_stage = 3;
            break;
        }
        case 3: {   /* walking left (208D) */
            app->girl_phase = app->girl_phase % 4 + 1;
            const unsigned pc[4][2] = {{0x197C, (unsigned)-0x3C8}, {0x1D10, (unsigned)-0x325}, {0x10C, 0},
                                       {legs_l[app->girl_phase - 1], 0x2D0}};
            girl_draw(app, pc, 4);
            if (app->girl_pos > 0x0C42) app->girl_pos -= 2; else app->girl_stage = 4;
            break;
        }
        case 4: {   /* turning back to the front (21A9) */
            const unsigned pc[4][2] = {{0x1FBC, (unsigned)-0x3C8}, {0x1E5C, (unsigned)-0x3C0}, {0x10C, 0}, {0x14EC, 0x2D0}};
            girl_draw(app, pc, 4); frames = 2; app->girl_stage = 5;
            break;
        }
        case 5: {
            const unsigned pc[3][2] = {{0x1EF4, (unsigned)-0x3C1}, {0, 0}, {0x1EC, 0x410}};
            girl_draw(app, pc, 3); frames = 2; app->girl_stage = 6;
            break;
        }
        case 6: {
            const unsigned pc[4][2] = {{0x197C, (unsigned)-0x3C8}, {0x1BC4, (unsigned)-0x325}, {0, 0}, {0x1EC, 0x410}};
            girl_draw(app, pc, 4); frames = 3; app->girl_stage = 7;
            break;
        }
        default:    /* done: on to the boxers (the tune stops, FGT 016E) */
            app->tune_cmd = -1;
            app_set_state(app, ST_PICK);
            return;
        }
        app->girl_wait_ms += frames * GIRL_FRAME_MS;
    }
}

static void draw_girl(App *app)
{
    anim_to_fb(&app->anim, &app->fb);
    /* FGT 1D35: the credits go in the scoreboard panels, printed past column
     * 40 like the names in a fight (see draw_fight) */
    static const struct { int col, row; const char *s; } cr[] = {
        {43, 1, "Program"}, {46, 2, "by"}, {42, 3, "Dave Murry"},
        {67, 1, "Animation"}, {70, 2, "by"}, {66, 3, "Barry Murry"},
    };
    for (size_t i = 0; i < sizeof cr / sizeof cr[0]; i++) {
        int b = cr[i].col * 2;
        ptxt_at(app, (b % 80) * 4, cr[i].row * 8 + (b / 80) * 2, WHITE, "%s", cr[i].s);
    }
    txt_at(app, 6, 23, WHITE, "SIERRA CHAMPIONSHIP BOXING");
    txt_at(app, 1, 24, WHITE, "(c) Copyright 1984 Sierra On-Line,Inc.");
}

static int typed_number(App *app, int key, int *out)
{
    size_t n = strlen(app->input);
    if (key >= '0' && key <= '9' && n < 3) { app->input[n] = (char)key; app->input[n + 1] = 0; }
    else if (key == KEY_BACKSPACE && n) app->input[n - 1] = 0;
    else if (key == KEY_ENTER && n) { *out = atoi(app->input); app->input[0] = 0; return 1; }
    return 0;
}

static void corners_then_fight(App *app);

static void begin_fight(App *app)
{
    Fight *f = &app->fight;
    f->rng_bytes = app->rng_code; f->rng_len = app->rng_len;
    f->rng_acc = (unsigned)(app->timer * 7 + 13);
    f->anim = &app->anim;
    /* the fight uses the records as edited on the Tale of the Tape */
    BoxerRecord keep[2] = {app->assets.roster[app->pick[0]], app->assets.roster[app->pick[1]]};
    app->assets.roster[app->pick[0]] = app->tape_rec[0];
    app->assets.roster[app->pick[1]] = app->tape_rec[1];
    fight_init(f, &app->assets, app->pick[0], app->pick[1]);
    app->assets.roster[app->pick[1]] = keep[1];
    app->assets.roster[app->pick[0]] = keep[0];
    anim_set_boxers(&app->anim, app->game_dir, f->b[0].rec.character - 1, f->b[1].rec.character - 1);
    app->state = ST_TAPE;   /* not a corner: corners_then_fight starts afresh */
    corners_then_fight(app);
}

static void open_tape(App *app)
{
    app->tape_rec[0] = app->assets.roster[app->pick[0]];
    app->tape_rec[1] = app->assets.roster[app->pick[1]];
    app->tape_edit = 0;
    app->tape_msg[0] = 0;
    app_set_state(app, ST_TAPE);
}

static void open_corner(App *app, int who)
{
    Fight *f = &app->fight;
    app->corner_who = who;
    app->corner_help = 0;
    app->corner_n = fight_corner_options(f, who, app->corner_lines, app->corner_keys);
    app->a_variant[who] = 1;
    for (int i = 0; i < app->corner_n; i++)
        if (app->corner_keys[i] == 'A' && strstr(app->corner_lines[i], "Jab")) app->a_variant[who] = 2;
    app_set_state(app, ST_CORNER);
}

/* Before each round: the corners. Humans choose (Strategy/Arcade), the
 * computer chooses for its own boxers. In Arcade the player's corner comes up
 * before the first round too, with its "Press O" prompt. */
static void corners_then_fight(App *app)
{
    Fight *f = &app->fight;
    int human_corner[2] = {0, 0};
    int arcade = app->assets.options.mode == 2;
    if (app->assets.options.mode >= 1) {
        int pl = app->assets.options.players;
        if (pl == 2) human_corner[0] = human_corner[1] = 1;
        else if (pl == 1) human_corner[app->assets.options.human_side == 1 ? 1 : 0] = 1;
    }
    if (app->state != ST_CORNER) {
        for (int i = 0; i < 2; i++)
            if (!human_corner[i] && f->round > 1) fight_apply_advice(f, i, fight_auto_advice(f, i), 1);
        if (human_corner[0] && (arcade || f->round > 1)) { open_corner(app, 0); return; }
        if (human_corner[1] && (arcade || f->round > 1)) { open_corner(app, 1); return; }
    } else if (app->corner_who == 0 && human_corner[1]) { open_corner(app, 1); return; }
    fight_start_round(f);
    start_round_anim(app);
    app_set_state(app, ST_FIGHT);
}

/* --------------------------------------------------------- Tale of the Tape */

/* FGT 17D6: how good the tape says a man is. */
static int tape_rating(const BoxerRecord *r)
{
    DerivedBoxer d;
    boxer_derive(r, &d);
    const Ratings *b = &d.base;
    int power = 10 * r->hitter;   /* before the knock-out bonus, which comes at the bell */
    int v = b->skill + power - 4 * b->vulnerable + 3 * b->aggression - r->age + b->defence +
            2 * (r->fights - r->losses) + 2 * (r->reach - 50);
    if (r->age > 33) v -= 20 * (r->age - 33);
    if (r->weight > 235) v -= r->weight / 5;
    return v;
}

/* FGT 1942: the betting line. Returns 0 for even odds, else 1 (left) or 2
 * (right) and the odds "a to b". */
static int tape_odds(const BoxerRecord *l, const BoxerRecord *r, int *a, int *b)
{
    int r0 = tape_rating(l), r1 = tape_rating(r);
    int d = l->weight - r->weight;
    if (d > 20 && l->weight < 235) r0 += 10 * d;
    d = -d;
    if (d > 20 && r->weight < 235) r1 += 10 * d;
    for (int guard = 0; guard < 64; guard++) {   /* the original could loop forever on 0 and 0 */
        if ((r1 > 5 && r0 > 5) || (r1 > 10 && r0 > 2) || (r0 > 10 && r1 > 2) || !((r0 & 1) || (r1 & 1))) {
            r1 /= 2; r0 /= 2;
        } else break;
    }
    if (r0 == 2 * r1) { r0 = 2; r1 = 1; }
    if (r1 == 2 * r0) { r1 = 2; r0 = 1; }
    if (r0 < 1) r0 = 1;
    if (r1 < 1) r1 = 1;
    if (r0 == r1) return 0;
    if (r0 > r1) { *a = r0; *b = r1; return 1; }
    *a = r1; *b = r0; return 2;
}

/* FGT 0ABC..1260: the twelve record items, where they are typed, their limits. */
static int *tape_field(BoxerRecord *r, int item)
{
    switch ((item - 1) % 6) {
    case 0: return &r->age;
    case 1: return &r->weight;
    case 2: return &r->reach;
    case 3: return &r->height;
    case 4: return &r->fights;
    default: return &r->losses;
    }
}

static void tape_key(App *app, int key)
{
    Options *o = &app->assets.options;
    int n;
    if (key == KEY_ESC) {
        if (app->tape_edit) { app->tape_edit = 0; app->input[0] = 0; }
        else app_set_state(app, ST_TITLE);
        return;
    }
    if (!typed_number(app, key, &n)) return;
    app->tape_msg[0] = 0;
    int item = app->tape_edit;
    if (item) {
        /* a new value for an item */
        app->tape_edit = 0;
        const char *bad = NULL;
        if (item == 13) {
            if (n < 1 || n > 15) bad = "Use a number between 1 and 15.";
            else o->rounds = n;
        } else {
            BoxerRecord *r = &app->tape_rec[item > 6];
            int k = (item - 1) % 6;
            if (k == 0 && (n < 17 || n > 50)) bad = "His age can be between 17 and 50.";
            else if (k == 1 && (n < 70 || n > 350)) bad = "Use a number between 70 and 350.";
            else if (k == 2 && (n < 40 || n > 99)) bad = "Use a number between 40 and 99.";
            else if (k == 3 && (n < 60 || n > 99))
                bad = item > 6 ? "That is not a reasonable height." : "Use a number between 60 and 99.";
            else if (k == 4 && n < r->losses) bad = "He's had more losses than that.";
            else if (k == 5 && n > r->fights) bad = "He hasn't had that many fights.";
            else *tape_field(r, item) = n;
        }
        if (bad) {
            snprintf(app->tape_msg, sizeof app->tape_msg, "%s", bad);
            app_sfx(app, SFX_ERROR);
            app->tape_edit = item;   /* FGT 0B2E: ask again */
        }
        return;
    }
    if (n >= 1 && n <= 13) { app->tape_edit = n; return; }
    switch (n) {
    case 14: o->round_secs = o->round_secs == 60 ? 180 : 60; break;
    case 15: o->mode = (o->mode + 1) % 3; break;
    case 16:
        if (o->mode == 0) { app_sfx(app, SFX_ERROR); break; }
        if (o->players == 0) o->players = 1, o->human_side = 0;
        else if (o->players == 1 && o->human_side == 0) o->human_side = 1;
        else if (o->players == 1) o->players = 2;
        else o->players = 0;
        break;
    case 17: app->pick[0] = app->pick[1] = -1; app->picking = 0; app_set_state(app, ST_PICK); break;
    case 18: app_set_state(app, ST_TITLE); break;
    case 19: begin_fight(app); break;
    default: app_sfx(app, SFX_ERROR); break;
    }
}

/* FGT 06DA..0A4B and 1942, the layout of the original. */
static void draw_tape(App *app)
{
    Options *o = &app->assets.options;
    const BoxerRecord *L = &app->tape_rec[0], *R = &app->tape_rec[1];
    uint8_t t = c_text(app), hi = text_mode(app) ? TXT_BRIGHT : 3;
    clear_fb(app);
    txt_at(app, 12, 0, hi, "TALE-OF-THE-TAPE");
    int a = 0, b = 0, who = tape_odds(L, R, &a, &b);
    if (!who) txt_at(app, 0, 3, t, "Odds are even on this fight.");
    else txt_at(app, 0, 3, t, "%s is a %d to %d favorite.", who == 1 ? L->name : R->name, a, b);
    txt_at(app, 6, 5, hi, "%s", L->name);
    txt_at(app, 26, 5, hi, "%s", R->name);
    static const char *lab[6] = {"Age ..... ", "Weight .. ", "Reach ... ", "Height .. ", "Fights .. ", "Losses .. "};
    for (int k = 0; k < 6; k++) {
        BoxerRecord *l = &app->tape_rec[0], *r = &app->tape_rec[1];
        txt_at(app, 0, 7 + k, t, "%d - %s%d", k + 1, lab[k], *tape_field(l, k + 1));
        txt_at(app, k < 3 ? 21 : 20, 7 + k, t, "%d - %s%d", k + 7, lab[k], *tape_field(r, k + 7));
    }
    int nr = o->rounds ? o->rounds : 12;
    txt_at(app, 0, 14, t, "13 - Scheduled for %d%s%s", nr, nr < 10 ? " " : "", nr == 1 ? " round. " : " rounds.");
    txt_at(app, 0, 15, t, "14 - Length of rounds: %s", o->round_secs == 60 ? "1 minute. " : "3 minutes.");
    static const char *mode[] = {"Simulation", "Strategy", "Arcade"};
    txt_at(app, 0, 16, t, "15 - %s Mode", mode[o->mode % 3]);
    if (o->mode != 0) {
        txt_at(app, 0, 17, t, "16 - Number of players: %d", o->players);
        if (o->players == 1) txt_at(app, 25, 17, t, "%s Side.", o->human_side ? "  Right" : "   Left");
    }
    txt_at(app, 0, 19, t, "17 - Choose new boxers.");
    txt_at(app, 0, 20, t, "18 - Go outside.");
    txt_at(app, 0, 21, hi, "19 - Begin the fight.");
    if (app->tape_edit) {
        /* typed where the value is (FGT 0ABC: column 14, or 35 on the right) */
        int it = app->tape_edit;
        int col = it == 13 ? 19 : it <= 6 ? 14 : 35, row = it == 13 ? 14 : 7 + (it - 1) % 6;
        txt_at(app, col, row, hi, "%-3s", "");
        txt_at(app, col, row, hi, "%s_", app->input);
        txt_at(app, 0, 23, t, "Select one of the above options: %d", it);
    } else txt_at(app, 0, 23, t, "Select one of the above options: %s_", app->input);
    if (app->tape_msg[0]) txt_at(app, 0, 24, t, "%s", app->tape_msg);
}

void app_key(App *app, int key)
{
    int n;
    Assets *a = &app->assets;
    Options *o = &a->options;
    switch (app->state) {
    case ST_ERROR:
        if (key == KEY_ESC || key == KEY_ENTER) app_set_state(app, ST_QUIT);
        break;
    case ST_TITLE:
        if (key == '1' || key == 'g' || key == 'G') app_set_state(app, ST_GYM_MENU);
        else if (key == '2' || key == 'f' || key == 'F') {
            app->pick[0] = app->pick[1] = -1; app->picking = 0;
            /* FGT 0144: the first time, with "Intro will play", the ring girl */
            if (!app->intro_done && app->assets.options.intro && app->anim.ok &&
                anim_load_girl(&app->anim, app->game_dir)) {
                app->intro_done = 1;
                app->girl_pos = 0x0C42; app->girl_phase = 0; app->girl_stage = 0; app->girl_wait_ms = 0;
                app->tune_cmd = 1;
                app_set_state(app, ST_GIRL);
                girl_tick(app);
            } else app_set_state(app, ST_PICK);
        }
        else if (key == '3' || key == 'o' || key == 'O') app_set_state(app, ST_OPTIONS);
        else if (key == '4' || key == 'e' || key == 'E' || key == KEY_ESC) app_set_state(app, ST_EXIT_CONFIRM);
        break;
    case ST_GIRL:   /* a key ends the intro (FGT 1FC4) */
        app->tune_cmd = -1;
        app_set_state(app, ST_PICK);
        break;
    case ST_EXIT_CONFIRM:
        if (key == 'y' || key == 'Y') app_set_state(app, ST_QUIT);
        else if (key == 'n' || key == 'N' || key == KEY_ESC) app_set_state(app, ST_TITLE);
        break;
    case ST_GYM_MENU:
        /* the Gym picture (PI2) carries the menu and its prompt */
        if (key == '2') app_set_state(app, ST_GYM);
        else if (key == '4' || key == KEY_ESC) app_set_state(app, ST_TITLE);
        else if (key == '1' || key == '3')
            snprintf(app->status, sizeof app->status, "%s is not in the remake yet.",
                     key == '1' ? "Making a boxer" : "Eliminating a boxer");
        break;
    case ST_GYM:
        if (key == 'm' || key == 'M' || key == KEY_ESC) app_set_state(app, ST_GYM_MENU);
        else if (typed_number(app, key, &n) && n >= 1 && n <= a->nboxers && a->roster[n - 1].used) {
            app->view_slot = n - 1; app_set_state(app, ST_GYM_VIEW);
        }
        break;
    case ST_GYM_VIEW:
        app_set_state(app, ST_GYM);
        break;
    case ST_OPTIONS:
        if (key == KEY_ESC || key == 'd' || key == 'D') { app_set_state(app, ST_TITLE); break; }
        if (!typed_number(app, key, &n)) break;
        switch (n) {
        case 2: o->sound = (o->sound + 1) % 3; break;
        case 3: o->bell = !o->bell; break;
        case 4: o->colours = (o->colours + 1) % 3; break;
        case 5: o->intro = !o->intro; break;
        case 6: o->scoring = !o->scoring; break;
        case 7: o->round_secs = o->round_secs == 60 ? 180 : 60; break;
        case 8: o->rounds = o->rounds % 15 + 1; break;
        case 9: o->mode = (o->mode + 1) % 3; break;
        case 11: app_set_state(app, ST_TITLE); break;   /* the EXIT sign on the picture */
        case 10:
            if (o->players == 0) o->players = 1, o->human_side = 1;
            else if (o->players == 1 && o->human_side == 1) o->human_side = 0;
            else if (o->players == 1) o->players = 2;
            else o->players = 0;
            break;
        }
        break;
    case ST_PICK:
        if (key == KEY_ESC) { app_set_state(app, ST_TITLE); break; }
        if (typed_number(app, key, &n)) {
            if (n >= 1 && n <= a->nboxers && a->roster[n - 1].used) {
                app->pick[app->picking++] = n - 1;
                if (app->picking == 2) open_tape(app);
            } else app_sfx(app, SFX_ERROR);   /* FGT 0270 */
        }
        break;
    case ST_TAPE:
        tape_key(app, key);
        break;
    case ST_FIGHT:
        if (key == KEY_ESC) app_set_state(app, ST_TITLE);
        break;
    case ST_STATS:
        if (app->timer >= STATS_DONE) corners_then_fight(app);
        break;
    case ST_CORNER: {
        if (app->assets.options.mode == 2) {
            /* Arcade: "Press O for arcade mode options or any other key" */
            if (app->corner_help) app->corner_help = 0;
            else if (key == 'o' || key == 'O') app->corner_help = 1;
            else corners_then_fight(app);
            break;
        }
        int k = (key >= 'a' && key <= 'z') ? key - 32 : key;
        for (int i = 0; i < app->corner_n; i++)
            if (app->corner_keys[i] == k) {
                fight_apply_advice(&app->fight, app->corner_who, (char)k, app->a_variant[app->corner_who]);
                corners_then_fight(app);
                return;
            }
        if (key == ' ' || key == KEY_ENTER) corners_then_fight(app);
        break;
    }
    case ST_RESULT: {
        int k = (key >= 'a' && key <= 'z') ? key - 32 : key;
        if (app->res_phase < 2) {   /* "Press any key for fight statistics." */
            if (app->res_phase == 1) { anim_cheer_end(&app->anim); app->res_phase = 2; app->res_prompt = 0; }
            break;
        }
        switch (app->res_prompt) {   /* OVR 455D..4757 */
        case 0:
            if (k == 'Y') app->res_prompt = 1;
            else if (k == 'N') app->res_prompt = 2;
            else app_sfx(app, SFX_ERROR);
            break;
        case 1:
            if (k == 'Y') app_set_state(app, ST_TAPE);   /* the same two men, as edited */
            else if (k == 'N') { app->pick[0] = app->pick[1] = -1; app->picking = 0; app_set_state(app, ST_PICK); }
            else app_sfx(app, SFX_ERROR);
            break;
        case 2:
            if (k == 'Q') app->res_prompt = 3;
            else if (k == 'S') app_set_state(app, ST_TITLE);
            else app_sfx(app, SFX_ERROR);
            break;
        case 3:
            if (k == 'Y') app_set_state(app, ST_QUIT);
            else app->res_prompt = 2;
            break;
        }
        break;
    }
    default: break;
    }
}

void app_action_key(App *app, int who, int action, int down)
{
    if (who < 0 || who > 1 || action <= ACT_NONE || action >= ACT_COUNT) return;
    app->held[action][who] = down;
    if (down) fight_press(&app->fight, who, action);
    int cur = ACT_NONE;
    for (int i = ACT_NONE + 1; i < ACT_COUNT; i++) if (app->held[i][who]) cur = i;
    app->fight.b[who].input = cur;
}

/* ------------------------------------------------------------------ draws */

static void draw_roster(App *app, int top)
{
    Assets *a = &app->assets;
    for (int i = 0; i < a->nboxers; i++) {
        if (!a->roster[i].name[0]) continue;
        int col = i < 20 ? 0 : i < 40 ? 13 : 27;
        int row = top + (i % 20);
        uint8_t c = i == app->pick[0] ? (text_mode(app) ? TXT_RED : MAGENTA) : c_text(app);
        txt_at(app, col, row, c, "%2d %-9s", i + 1, a->roster[i].name);   /* GYM 2610: number, space, name */
    }
}

static const char *rating_word(int v)
{
    static const char *w[] = {"?", "Almost never", "Sometimes", "Usually", "Almost always"};
    return (v >= 1 && v <= 4) ? w[v] : w[0];
}

static void draw_stats(App *app, const BoxerRecord *r)
{
    static const char *tires[] = {"?", "first few rounds", "middle rounds", "last rounds"};
    static const char *hit[] = {"?", "Light", "Average", "Hard"};
    static const char *start[] = {"?", "Fast", "Average", "Slow"};
    txt_at(app, 0, 0, MAGENTA, "%s'S STATISTICS", r->name);
    txt_at(app, 0, 2, WHITE, "1 - Age ........... %d", r->age);
    txt_at(app, 0, 3, WHITE, "2 - Height ........ %d", r->height);
    txt_at(app, 0, 4, WHITE, "3 - Weight ........ %d", r->weight);
    txt_at(app, 0, 5, WHITE, "4 - Reach ......... %d", r->reach);
    txt_at(app, 0, 6, WHITE, "5 - Fights ........ %d", r->fights);
    txt_at(app, 0, 7, WHITE, "6 - Losses ........ %d", r->losses);
    txt_at(app, 0, 8, WHITE, "7 - KO opponent ... %d", r->kos);
    txt_at(app, 0, 9, WHITE, "8 - KO defeats .... %d", r->ko_defeats);
    txt_at(app, 0, 10, WHITE, "9 - Best punch .... %s", punch_name(r->best_punch));
    txt_at(app, 0, 11, WHITE, "10 - Tires during %s", tires[r->tires >= 1 && r->tires <= 3 ? r->tires : 0]);
    txt_at(app, 0, 12, WHITE, "11 - %s hitter", hit[r->hitter >= 1 && r->hitter <= 3 ? r->hitter : 0]);
    txt_at(app, 0, 13, WHITE, "12 - %s absorbs a punch", rating_word(r->absorbs));
    txt_at(app, 0, 14, WHITE, "13 - %s is injured", rating_word(r->injured));
    txt_at(app, 0, 15, WHITE, "14 - %s starter", start[r->starter >= 1 && r->starter <= 3 ? r->starter : 0]);
    txt_at(app, 0, 16, WHITE, "15 - %s goes in", rating_word(r->killer));
    txt_at(app, 0, 17, WHITE, "     for the kill");
    txt_at(app, 0, 18, WHITE, "16 - %s hits what", rating_word(r->accuracy));
    txt_at(app, 0, 19, WHITE, "     he aims for");
    txt_at(app, 0, 20, WHITE, "17 - %s is aggressive", rating_word(r->aggressive));
    txt_at(app, 0, 21, WHITE, "18 - %s uses good defense", rating_word(r->defence));
    txt_at(app, 0, 22, WHITE, "19 - %s uses fast punches", rating_word(r->hand_speed));
    txt_at(app, 0, 23, WHITE, "20 - %s character", character_name(r->character));
}

/* OPT 014C..0453: the original's layout, one option per text row from row 3,
 * column 3, with its own strings (OPT data 0002..016F). */
static void draw_options(App *app)
{
    Options *o = &app->assets.options;
    static const char *snd[] = {"off     ", "Internal", "External"};
    static const char *col[] = {" set 1", "s will alternate", " set 2"};
    static const char *mode[] = {"Simulation", "Strategy", "Arcade"};
    show_screen(app, SCR_OPTIONS);
    txt_at(app, 3, 3, WHITE, "1. Center the screen");
    txt_at(app, 3, 4, WHITE, "2. Sound is %s", snd[o->sound % 3]);
    txt_at(app, 3, 5, WHITE, "3. Bell will %s", o->bell ? "ring" : "beep");
    txt_at(app, 3, 6, WHITE, "4. Color%s", col[o->colours % 3]);
    txt_at(app, 3, 7, WHITE, "5. Intro will %splay", o->intro ? "" : "not ");
    txt_at(app, 3, 8, WHITE, "6. Scoring system: %s", o->scoring ? "Round" : "Ten Point Must");
    txt_at(app, 3, 9, WHITE, "7. Each round is %s", o->round_secs == 60 ? "1 minute" : "3 minutes");
    txt_at(app, 3, 10, WHITE, "8. Number of rounds: %d", o->rounds ? o->rounds : 12);
    txt_at(app, 3, 11, WHITE, "9. %s Mode", mode[o->mode % 3]);
    if (o->mode != 0) {   /* no players line in Simulation */
        if (o->players == 1)
            txt_at(app, 2, 12, WHITE, "10. Number of players: 1%s side", o->human_side == 1 ? " Right" : "  Left");
        else
            txt_at(app, 2, 12, WHITE, "10. Number of players: %d", o->players);
    }
    char typed[3] = "__";
    for (int i = 0; i < 2 && app->input[i]; i++) typed[i] = app->input[i];
    txt_at(app, 0, 24, WHITE, "   SELECT ONE OF THE ABOVE OPTIONS: %s", typed);
}

/* The original prints no names, energy or time during a round: the clock
 * face shows the time (FG2 2573) and only "ROUND"/"COUNT" and a number appear
 * under the ring (FG2 07AB, 21F2, 226D: column 17 row 22, column 19 row 24).
 * The end of a fight is announced on rows 22 and 23 from column 0 (0C4D). */
static void draw_fight(App *app)
{
    Fight *f = &app->fight;
    if (app->anim.ok && app->anim.have_boxers) anim_to_fb(&app->anim, &app->fb);
    else show_screen(app, SCR_ARENA);
    /* FG2 130B: the names (centred in 8, a space more when the score has
     * three digits) and the scores in the two panels. It prints at text
     * columns 83/107 and 46/70, which in CGA memory land at these pixels. */
    for (int i = 0; i < 2; i++) {
        const char *nm = f->b[i].rec.name;
        int pad = (8 - (int)strlen(nm)) / 2 + (f->total[i] > 99);
        if (pad < 0) pad = 0;
        ptxt_at(app, (i ? 216 : 24) + 8 * pad, 12, WHITE, "%s", nm);
        ptxt_at(app, i ? 240 : 48, 26, WHITE, "%d", f->total[i]);
    }
    if (f->result) {
        /* FG2 0C0F..1106: rows 22 and 24 are cleared; a stoppage keeps the
         * referee's two lines; a decision first shows the judges adding up */
        if (f->result == RES_TKO) {
            txt_at(app, 0, 22, WHITE, "%s", f->line1);
            txt_at(app, 0, 23, WHITE, "%s", f->line2);
        } else if (app->state == ST_RESULT && app->res_phase == 0 && (f->result == RES_DECISION || f->result == RES_DRAW)) {
            txt_at(app, 0, 22, WHITE, "  Judges are adding their score cards  ");
        } else if (f->result == RES_DRAW) {
            txt_at(app, 8, 22, WHITE, "THIS FIGHT IS A DRAW!");
        } else if (f->winner >= 0) {
            txt_at(app, 5, 22, WHITE, "WINNER BY A %s: %s", f->result == RES_KO ? "KNOCKOUT" : "DECISION",
                   f->b[f->winner].rec.name);
        }
        if (app->state == ST_RESULT && app->res_phase == 1)
            txt_at(app, 2, 24, WHITE, "Press any key for fight statistics.");
    } else if (f->count > 0) {
        txt_at(app, 17, 22, WHITE, "COUNT");
        if (f->count > 1) txt_at(app, 19, 24, WHITE, "%-3d", f->count - 1);
    } else {
        txt_at(app, 17, 22, WHITE, "ROUND");
        txt_at(app, 19, 24, WHITE, "%d", f->round);
    }
}

static void draw_corner(App *app)
{
    Fight *f = &app->fight;
    int w = app->corner_who;
    clear_fb(app);
    if (app->assets.options.mode == 2) {
        /* Arcade: the corner only offers the arcade options (FIGHT.OLY) */
        uint8_t t = c_text(app);
        char title[40];
        snprintf(title, sizeof title, "%s's CORNER", f->b[w].rec.name);
        txt_at(app, (40 - (int)strlen(title)) / 2, 0, t, "%s", title);
        if (!app->corner_help) {
            txt_at(app, 3, 11, t, "Press \"O\" for arcade mode options");
            txt_at(app, 3, 13, t, "or ... any other key to continue.");
            return;
        }
        /* The original's options (remapping keys, style, handicap) are not
         * in the remake yet: show its keys instead. */
        uint8_t h = c_head(app);
        txt_at(app, 0, 2, h, "KEYS IN THE REMAKE");
        txt_at(app, 0, 4, t, "              LEFT  RIGHT (keypad)");
        txt_at(app, 0, 5, t, "Jab            Q     7");
        txt_at(app, 0, 6, t, "Head punch     W     8   or Up");
        txt_at(app, 0, 7, t, "Uppercut       E     9");
        txt_at(app, 0, 8, t, "Body punch     S     5");
        txt_at(app, 0, 9, t, "Duck           Z     2   or Down");
        txt_at(app, 0, 10, t, "Cover head     X     1");
        txt_at(app, 0, 11, t, "Guard (body)   C     3");
        txt_at(app, 0, 12, t, "Step forward   D     4   or Left");
        txt_at(app, 0, 13, t, "Step back      A     6   or Right");
        txt_at(app, 0, 15, t, "The left boxer also has the original's");
        txt_at(app, 0, 16, t, "keys: F1 back, F2 forward, F3 jab,");
        txt_at(app, 0, 17, t, "F4 head, F5 body, F6 uppercut, F7");
        txt_at(app, 0, 18, t, "guard, F8 cover, F9/F10 duck.");
        txt_at(app, 0, 24, t, "Press any key.");
        return;
    }
    txt_at(app, 0, 0, MAGENTA, "%s's CORNER", f->b[w].rec.name);
    for (int i = 0; i < 2; i++) {
        txt_at(app, 0, 2 + i * 3, WHITE, "%s's status -", f->b[i].rec.name);
        txt_at(app, 2, 3 + i * 3, CYAN, "%s", energy_text(f->b[i].live.energy));
        txt_at(app, 2, 4 + i * 3, CYAN, "%s", injury_text(f->b[i].injury));
    }
    txt_at(app, 0, 9, WHITE, "Score: %d - %d", f->total[0], f->total[1]);
    txt_at(app, 0, 11, MAGENTA, "Your advice:");
    for (int i = 0; i < app->corner_n; i++) txt_at(app, 0, 13 + i, WHITE, "%s", app->corner_lines[i]);
    txt_at(app, 0, 23, CYAN, "Select one of the above options:");
}

/* FG2 1462: the end-of-round statistics on the STAT picture, the judges'
 * cards turned over one at a time (pauses of 25000 and 10000 calls). */
static void draw_stats_screen(App *app)
{
    Fight *f = &app->fight;
    show_screen(app, SCR_STATS);
    for (int i = 0; i < 2; i++) {
        const FightBoxer *b = &f->b[i];
        txt_at(app, 0, 4 + 5 * i, WHITE, "%s's status -", b->rec.name);
        txt_at(app, 20, 4 + 5 * i, WHITE, "%s", energy_text(b->live.energy));
        txt_at(app, 20, 5 + 5 * i, WHITE, "%s", injury_text(b->injury));
        txt_at(app, 1, 19 + 2 * i, WHITE, "%s", b->rec.name);
    }
    txt_at(app, 27, 15, WHITE, "%d", f->round - 1);
    for (int j = 0; j < 3; j++) {
        int t0 = j * STATS_STEP + 11;
        if (app->timer >= t0) txt_at(app, 10 * j + 13, 19, WHITE, "%2d", f->judge_round[j][0]);
        if (app->timer >= t0 + 5) txt_at(app, 10 * j + 13, 21, WHITE, "%2d", f->judge_round[j][1]);
    }
    if (app->timer >= STATS_DONE) {
        /* SierraCh: "Press any key for <name>'s corner" / "the next round." */
        char line[48];
        int who = -1;
        Options *o = &app->assets.options;
        if (o->mode >= 1 && o->players == 2) who = 0;
        else if (o->mode >= 1 && o->players == 1) who = o->human_side == 1 ? 1 : 0;
        if (who >= 0) snprintf(line, sizeof line, "Press any key for %s's corner", f->b[who].rec.name);
        else snprintf(line, sizeof line, "Press any key for the next round.");
        txt_at(app, (41 - (int)strlen(line)) / 2, 24, WHITE, "%s", line);
    }
}

/* OVR 411F: the end-of-fight statistics on the STAT picture. */
static void draw_result(App *app)
{
    Fight *f = &app->fight;
    show_screen(app, SCR_STATS);
    ptxt_at(app, 128, 2, WHITE, "FIGHT");                 /* (56,0), over "ROUND" */
    txt_at(app, 11, 15, WHITE, "JUDGE SCORE CARDS");
    if (f->winner >= 0) txt_at(app, 11, 2, WHITE, "WINNER: %s", f->b[f->winner].rec.name);
    else txt_at(app, 11, 2, WHITE, "WINNER: DRAW");
    txt_at(app, 11, 3, WHITE, "ROUNDS: %d", f->round);
    for (int i = 0; i < 2; i++) {
        const FightBoxer *b = &f->b[i];
        int row = 5 + 5 * i;
        txt_at(app, 0, row, WHITE, "%s:", b->rec.name);
        if (f->result == RES_KO && f->winner == !i) txt_at(app, 10, row, WHITE, "Knocked Out.          ");
        else txt_at(app, 10, row, WHITE, "%s", injury_text(b->injury));
        if (b->kd_total) txt_at(app, 10, row + 1, WHITE, "Knocked down %d time%s.", b->kd_total, b->kd_total > 1 ? "s" : "");
        else txt_at(app, 10, row + 1, WHITE, "Was not knocked down.");
        txt_at(app, 1, 19 + 2 * i, WHITE, "%s", b->rec.name);
        for (int j = 0; j < 3; j++) txt_at(app, 10 * j + 13, 19 + 2 * i, WHITE, "%2d", f->judge_total[j][i]);
    }
    static const struct { int col; const char *s; } q[4] = {
        {0, " Another Fight? (Y or N) _"}, {8, "Rematch? (Y or N) _"},
        {5, "Quit or Start over? (Q or S) _"}, {5, "QUIT - are you sure? (Y or N) _"}};
    txt_at(app, q[app->res_prompt].col, 24, WHITE, "%s", q[app->res_prompt].s);
}

/* FG2 1165: the loser's frame and the winner standing, as the result is
 * announced. */
static void result_poses(App *app)
{
    Fight *f = &app->fight;
    Anim *a = &app->anim;
    if (!a->ok || !a->have_boxers) return;
    int w = f->winner < 0 ? 3 : f->winner;
    int ko = f->result == RES_KO;
    if (w == 0) anim_frame_at(a, 1, a->end_pose[ko][1]);
    else anim_frame_at(a, 0, a->end_pose[ko][0]);
    if (w < 3) anim_frame_at(a, w, a->seq[SEQ_IDLE][w]);
}

static int res_rnd(void *ctx) { return fight_random((Fight *)ctx); }

/* FG2 0DD5: pauses of 60000 and 40000 units while the judges add up */
#define JUDGES_TICKS (100000 / UNITS_PER_TICK)
#define CHEER_UNITS 4000

void app_tick(App *app)
{
    Assets *a = &app->assets;
    Fight *f = &app->fight;
    app->timer++;
    txt_clear(app);
    app->palette = 1;

    switch (app->state) {
    case ST_ERROR:
        clear_fb(app);
        txt_at(app, 0, 1, WHITE, "Championship Boxing remake");
        txt_at(app, 0, 3, MAGENTA, "Could not find the game files.");
        txt_at(app, 0, 5, WHITE, "Copy your own BOXING.* files into a");
        txt_at(app, 0, 6, WHITE, "folder and start with:");
        txt_at(app, 0, 8, CYAN, " cbremake --game-dir <folder>");
        txt_at(app, 0, 10, WHITE, "or set CB_GAME_DIR, or put them in");
        txt_at(app, 0, 11, WHITE, "Original_IBM_CGA next to the program.");
        if (a->missing[0]) txt_at(app, 0, 13, WHITE, "Missing: %.30s", a->missing);
        txt_at(app, 0, 24, WHITE, "Press Esc to quit.");
        break;
    case ST_TITLE:
        show_screen(app, SCR_TITLE);   /* the picture already shows 1-4 */
        break;
    case ST_GIRL:
        app->palette = app->assets.options.colours == 2 ? 0 : 1;   /* FGT 1CAE */
        app->girl_wait_ms -= 100;
        girl_tick(app);
        if (app->state == ST_GIRL) draw_girl(app);
        break;
    case ST_EXIT_CONFIRM:
        show_screen(app, SCR_TITLE);
        txt_at(app, 0, 24, WHITE, "EXIT - are you sure? (Y or N) _");
        break;
    case ST_GYM:
        clear_fb(app);
        txt_at(app, 12, 0, c_head(app), "AVAILABLE BOXERS");
        draw_roster(app, 2);
        txt_at(app, 0, 23, c_text(app), "Boxer to look at: %s_", app->input);
        txt_at(app, 0, 24, CYAN, "Enter \"M\" to go back to the main gym.");
        break;
    case ST_GYM_MENU:
        show_screen(app, SCR_GYM);
        if (app->status[0]) txt_at(app, 0, 23, WHITE, "%-40s", app->status);
        break;
    case ST_GYM_VIEW:
        clear_fb(app);
        draw_stats(app, &a->roster[app->view_slot]);
        txt_at(app, 0, 24, CYAN, "Press any key.");
        break;
    case ST_OPTIONS:
        draw_options(app);
        break;
    case ST_PICK:
        clear_fb(app);
        txt_at(app, 12, 0, c_head(app), "AVAILABLE BOXERS");
        draw_roster(app, 2);
        txt_at(app, 0, 23, c_text(app), "Select a boxer for the %s corner: %s_",
               app->picking ? "right" : "left", app->input);
        break;
    case ST_TAPE:
        draw_tape(app);
        break;
    case ST_FIGHT:
        app->palette = fight_palette(app);
        if (fight_tick(f)) {
            if (!f->result) fight_end_round(f);
            app_set_state(app, f->result ? ST_RESULT : ST_STATS);
            app->res_phase = 0;
        }
        for (int i = 0; i < f->nsfx; i++) app_sfx(app, f->sfx[i]);
        f->nsfx = 0;
        if (app->state == ST_FIGHT) draw_fight(app);
        else if (app->state == ST_RESULT) draw_fight(app);
        else draw_stats_screen(app);
        break;
    case ST_STATS:
        draw_stats_screen(app);
        break;
    case ST_CORNER:
        draw_corner(app);
        break;
    case ST_RESULT:
        if (app->res_phase == 2) { draw_result(app); break; }
        app->palette = fight_palette(app);
        if (app->res_phase == 0 && (f->result != RES_DECISION && f->result != RES_DRAW ? 1 : app->timer >= JUDGES_TICKS)) {
            app->res_phase = 1;
            app->res_units = 0;
            result_poses(app);
        }
        if (app->res_phase == 1)
            for (app->res_units += UNITS_PER_TICK; app->res_units >= CHEER_UNITS; app->res_units -= CHEER_UNITS)
                anim_cheer(&app->anim, f->winner, res_rnd, f);
        draw_fight(app);
        break;
    default: break;
    }
}

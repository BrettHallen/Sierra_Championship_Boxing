/* Championship Boxing remake: SDL3 front end.
 *
 *   cbremake [--game-dir DIR] [--scale N] [--selftest] [--shots DIR]
 *
 * The game files (BOXING.*) come from the player's own copy. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "audio.h"

typedef struct { SDL_Keycode key; int who, action; } ActionKey;

/* Arcade controls. Left boxer on the letters or, as in the original (FG2
 * 1DA8), on F1-F10; right boxer on the keypad / arrows (the original lets
 * you remap these from the corner screen). */
static const ActionKey action_keys[] = {
    {SDLK_Q, 0, ACT_JAB},       {SDLK_W, 0, ACT_HEAD},      {SDLK_E, 0, ACT_UPPERCUT},
    {SDLK_S, 0, ACT_BODY},      {SDLK_Z, 0, ACT_DUCK},      {SDLK_X, 0, ACT_COVER_HEAD},
    {SDLK_C, 0, ACT_COVER_BODY},{SDLK_D, 0, ACT_FORWARD},   {SDLK_A, 0, ACT_BACK},
    {SDLK_KP_7, 1, ACT_JAB},    {SDLK_KP_8, 1, ACT_HEAD},   {SDLK_KP_9, 1, ACT_UPPERCUT},
    {SDLK_KP_5, 1, ACT_BODY},   {SDLK_KP_2, 1, ACT_DUCK},   {SDLK_KP_1, 1, ACT_COVER_HEAD},
    {SDLK_KP_3, 1, ACT_COVER_BODY}, {SDLK_KP_4, 1, ACT_FORWARD}, {SDLK_KP_6, 1, ACT_BACK},
    {SDLK_LEFT, 1, ACT_FORWARD},{SDLK_RIGHT, 1, ACT_BACK},  {SDLK_UP, 1, ACT_HEAD},
    {SDLK_DOWN, 1, ACT_DUCK},
    {SDLK_F1, 0, ACT_BACK},     {SDLK_F2, 0, ACT_FORWARD},  {SDLK_F3, 0, ACT_JAB},
    {SDLK_F4, 0, ACT_HEAD},     {SDLK_F5, 0, ACT_BODY},     {SDLK_F6, 0, ACT_UPPERCUT},
    {SDLK_F7, 0, ACT_COVER_BODY}, {SDLK_F8, 0, ACT_COVER_HEAD}, {SDLK_F9, 0, ACT_DUCK},
    {SDLK_F10, 0, ACT_DUCK},
};

/* Text colours: 0-3 the CGA palette, 0x10 + n the 16 text-mode colours. */
static Rgb text_rgb(const Rgb pal[4], uint8_t c)
{
    static const Rgb ega[16] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}};
    if (c & 0x10) return ega[c & 15];
    if (c == 0) return pal[3];
    return pal[c & 3];
}

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *frame;   /* 320x200 streaming texture for the CGA framebuffer */
    SDL_Texture *target;  /* 320x200 render target: frame + text */
} Video;

static void render(App *app, Video *v)
{
    Rgb pal[4];
    cga_palette(app->palette, 1, pal);
    void *pixels; int pitch;
    if (SDL_LockTexture(v->frame, NULL, &pixels, &pitch)) {
        for (int y = 0; y < CGA_H; y++) {
            Uint32 *row = (Uint32 *)((Uint8 *)pixels + y * pitch);
            for (int x = 0; x < CGA_W; x++) {
                Rgb c = pal[app->fb.px[y * CGA_W + x] & 3];
                row[x] = 0xFF000000u | (Uint32)c.r << 16 | (Uint32)c.g << 8 | c.b;
            }
        }
        SDL_UnlockTexture(v->frame);
    }
    SDL_SetRenderTarget(v->renderer, v->target);
    SDL_RenderTexture(v->renderer, v->frame, NULL, NULL);
    for (int r = 0; r < TEXT_ROWS; r++)
        for (int c = 0; c < TEXT_COLS; c++) {
            char ch = app->text[r][c];
            if (app->text_colour[r][c] == 0xFF) continue;   /* nothing written here */
            Rgb col = text_rgb(pal, app->text_colour[r][c]);
            SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
            SDL_FRect bg = {(float)c * 8, (float)r * 8, 8, 8};
            SDL_RenderFillRect(v->renderer, &bg);
            if (ch == ' ') continue;
            SDL_SetRenderDrawColor(v->renderer, col.r, col.g, col.b, 255);
            char s[2] = {ch, 0};
            SDL_RenderDebugText(v->renderer, (float)c * 8, (float)r * 8, s);
        }
    for (int i = 0; i < app->nptext; i++) {
        Rgb col = text_rgb(pal, app->ptext[i].colour);
        SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
        SDL_FRect bg = {(float)app->ptext[i].x, (float)app->ptext[i].y, 8.0f * (float)strlen(app->ptext[i].s), 8};
        SDL_RenderFillRect(v->renderer, &bg);
        SDL_SetRenderDrawColor(v->renderer, col.r, col.g, col.b, 255);
        SDL_RenderDebugText(v->renderer, (float)app->ptext[i].x, (float)app->ptext[i].y, app->ptext[i].s);
    }
    SDL_SetRenderTarget(v->renderer, NULL);
    SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
    SDL_RenderClear(v->renderer);
    SDL_RenderTexture(v->renderer, v->target, NULL, NULL);  /* letterboxed to 4:3 */
}

static int save_shot(Video *v, const char *dir, const char *name)
{
    SDL_SetRenderTarget(v->renderer, v->target);
    SDL_Surface *s = SDL_RenderReadPixels(v->renderer, NULL);
    SDL_SetRenderTarget(v->renderer, NULL);
    if (!s) return 0;
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
    int ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    printf("wrote %s\n", path);
    return ok;
}

/* The window and Dock icon: the ring-girl intro frame from the player's own
 * GIRL.SPT, shown 4:3 like the screen and centred on a square. Nothing of
 * the original is stored in the remake. Returns the icon (for --shots). */
static SDL_Surface *set_icon(App *app, Video *v)
{
    if (!app_icon_scene(app)) return NULL;
    render(app, v);
    SDL_SetRenderTarget(v->renderer, v->target);
    SDL_Surface *frame = SDL_RenderReadPixels(v->renderer, NULL);
    SDL_SetRenderTarget(v->renderer, NULL);
    txt_clear(app);
    if (!frame) return NULL;
    SDL_Surface *icon = SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_ARGB8888);
    if (icon) {
        SDL_FillSurfaceRect(icon, NULL, 0);   /* transparent above and below */
        SDL_Rect r = {0, 32, 256, 192};
        SDL_BlitSurfaceScaled(frame, NULL, icon, &r, SDL_SCALEMODE_LINEAR);
        SDL_SetWindowIcon(v->window, icon);
    }
    SDL_DestroySurface(frame);
    return icon;
}

/* Headless check: load everything and run a computer-vs-computer fight. */
static int selftest(const char *dir)
{
    App *app = SDL_calloc(1, sizeof *app);
    app_init(app, dir);
    Assets *a = &app->assets;
    if (app->state == ST_ERROR) {
        printf("game files not found or incomplete. missing: %s\n", a->missing);
        SDL_free(app);
        return 1;
    }
    printf("game folder: %s\n", a->dir);
    printf("missing files: %s\n", a->missing[0] ? a->missing : "(none)");
    for (int c = 0; c < CHAR_COUNT; c++)
        for (int s = 0; s < 2; s++) {
            BoxerSprites *b = &a->boxers[c][s];
            printf("boxer set %d/%d: %d body, %d head, %d arm frames%s\n", c, s,
                   b->nbody, b->nhead, b->narms, b->mirrored ? " (mirrored)" : "");
        }
    printf("ring girl frames: %d, fight extras: %d, boxers: %d\n", a->ngrl, a->nfg3, a->nboxers);
    printf("animation tables: %s\n", app->anim.ok ? "found" : "NOT FOUND");
    int left = -1, right = -1;
    for (int i = 0; i < a->nboxers; i++)
        if (a->roster[i].used) { if (left < 0) left = i; else if (right < 0) { right = i; break; } }
    Fight *f = &app->fight;
    if (anim_set_boxers(&app->anim, a->dir, a->roster[left].character - 1, a->roster[right].character - 1)) {
        anim_reset(&app->anim);
        for (int t = 0; t < SEQ_COUNT; t++) {   /* play every sequence once */
            for (int s = 0; s < 2; s++) {
                anim_play(&app->anim, s, t);
                while (anim_busy(&app->anim, s)) anim_step(&app->anim, s);
            }
        }
        printf("boxer animation: all %d sequences played%s%s\n", SEQ_COUNT,
               app->anim.note[0] ? "; " : "", app->anim.note);
    }
    printf("falls: %04X/%04X %04X/%04X\n", app->anim.seq[SEQ_FALL_ROPES][0], app->anim.seq[SEQ_FALL_ROPES][1],
           app->anim.seq[SEQ_FALL_SIT][0], app->anim.seq[SEQ_FALL_SIT][1]);
    f->rng_bytes = app->rng_code; f->rng_len = app->rng_len; f->rng_acc = 1;
    f->anim = &app->anim;
    a->options.mode = 0;
    fight_init(f, a, left, right);
    for (int i = 0; i < 2; i++) {
        Ratings *r = &f->b[i].der.base;
        printf("%-9s skill %d aggr %d open %d def %d energy %d tough %d power %d vuln %d head %d\n",
               f->b[i].rec.name, r->skill, r->aggression, r->openness, r->defence, r->energy,
               r->toughness, r->power, r->vulnerable, r->head_target);
    }
    while (!f->result) {
        while (!fight_tick(f)) {}
        if (f->result) break;
        fight_end_round(f);
        printf("round %d: %d-%d  energy %d/%d  injuries %d/%d  kd %d/%d  landed %d/%d\n", f->round - (f->result ? 0 : 1),
               f->judge_round[0][0], f->judge_round[0][1], f->b[0].live.energy, f->b[1].live.energy,
               f->b[0].injury, f->b[1].injury, f->b[0].kd_total, f->b[1].kd_total,
               f->b[0].punches_landed, f->b[1].punches_landed);
        if (f->result) break;
        for (int i = 0; i < 2; i++) fight_apply_advice(f, i, fight_auto_advice(f, i), 1);
        fight_start_round(f);
    }
    printf("result %d winner %d after %d rounds (%s / %s)\n", f->result, f->winner, f->round,
           f->line1, f->line2);
    app_free(app);
    SDL_free(app);
    return 0;
}

static void play_sounds(App *app)
{
    if (app->tune_cmd > 0) audio_tune((const unsigned short (*)[2])app->tune, app->ntune, app->assets.options.sound);
    else if (app->tune_cmd < 0) audio_stop();
    app->tune_cmd = 0;
    for (int i = 0; i < app->nsfx; i++)
        audio_play(app->sfx[i], app->assets.options.sound, app->assets.options.bell);
    app->nsfx = 0;
}

int main(int argc, char **argv)
{
    const char *dir = NULL, *shots = NULL;
    int scale = 3, test = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = SDL_atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest")) test = 1;
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (argv[i][0] != '-') dir = argv[i];
        else { printf("usage: %s [--game-dir DIR] [--scale N] [--selftest] [--shots DIR]\n", argv[0]); return 1; }
    }
    if (test) return selftest(dir);

    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (scale < 1) scale = 1;
    Video v = {0};
    if (!SDL_CreateWindowAndRenderer("Sierra Championship Boxing (IBM/CGA)", 320 * scale, 240 * scale,
                                     SDL_WINDOW_RESIZABLE, &v.window, &v.renderer)) {
        fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(v.renderer, 1);
    /* CGA pixels are taller than wide: show 320x200 on a 4:3 screen. */
    SDL_SetRenderLogicalPresentation(v.renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    v.frame = SDL_CreateTexture(v.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, CGA_W, CGA_H);
    v.target = SDL_CreateTexture(v.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, CGA_W, CGA_H);
    SDL_SetTextureScaleMode(v.frame, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(v.target, SDL_SCALEMODE_NEAREST);

    App *app = SDL_calloc(1, sizeof *app);
    app_init(app, dir);
    SDL_Surface *icon = set_icon(app, &v);
    if (icon && shots) {
        char path[1200];
        snprintf(path, sizeof path, "%s/00_icon.bmp", shots);
        SDL_SaveBMP(icon, path);
    }
    SDL_DestroySurface(icon);
    if (!shots && !audio_init()) fprintf(stderr, "no sound: %s\n", SDL_GetError());
    if (shots) {
        /* Walk through the screens with scripted keys and save each one. */
        static const struct { int key; int ticks; const char *name; } script[] = {
            {0, 1, "01_title"}, {'3', 1, "04_options"}, {KEY_ESC, 1, NULL},
            {'2', 8, "04b_girl"}, {0, 40, "04c_girl"}, {0, 40, "04d_girl"}, {0, 300, "05_pick"}, {'1', 0, NULL}, {KEY_ENTER, 0, NULL}, {'2', 0, NULL},
            {KEY_ENTER, 1, "06_tape"}, {'1', 0, NULL}, {'9', 0, NULL}, {KEY_ENTER, 1, "07_corner"},
            {' ', 8, "08_touch_gloves"}, {0, 30, "08b_opening"}, {0, 60, "09_fight"}, {0, 200, "09b_fight"},
            {0, 3000, NULL},
        };
        /* CB_SHOTS_SIM: a Simulation fight (Tale of the Tape item 15) to its end */
        static const struct { int key; int ticks; const char *name; } sim[] = {
            {'2', 1, NULL}, {' ', 1, NULL}, {'1', 0, NULL}, {KEY_ENTER, 0, NULL}, {'2', 0, NULL}, {KEY_ENTER, 1, NULL},
            {'1', 0, NULL}, {'5', 0, NULL}, {KEY_ENTER, 1, "s06_tape_sim"}, {'1', 0, NULL}, {'9', 0, NULL},
            {KEY_ENTER, 30, "s08_round1"}, {0, 9000, NULL},
        };
        int use_sim = SDL_getenv("CB_SHOTS_SIM") != NULL;
        size_t nscript = use_sim ? sizeof sim / sizeof sim[0] : sizeof script / sizeof script[0];
        for (size_t i = 0; i < nscript; i++) {
            int key = use_sim ? sim[i].key : script[i].key, ticks = use_sim ? sim[i].ticks : script[i].ticks;
            const char *name = use_sim ? sim[i].name : script[i].name;
            if (use_sim && app->state == ST_RESULT && app->res_phase == 2 && i == nscript - 1) break;
            if (key) app_key(app, key);
            for (int t = 0; t < ticks; t++) {
                app_tick(app);
                if (use_sim && app->state == ST_RESULT) {
                    static int shot;
                    if (app->res_phase == 1 && app->timer % 6 == 0 && shot < 4) {
                        char nm[32]; SDL_snprintf(nm, sizeof nm, "s11_ring_%d", shot++); render(app, &v); save_shot(&v, shots, nm);
                    }
                    if (shot == 4) { app_key(app, ' '); shot++; }
                    if (app->res_phase == 2 && shot == 5) {
                        app_tick(app); render(app, &v); save_shot(&v, shots, "s12_fight_stats");
                        app_key(app, 'N'); app_tick(app); render(app, &v); save_shot(&v, shots, "s13_quit_or_start");
                        shot++; t = ticks; break;
                    }
                    continue;
                }
                if (app->state == ST_STATS && app->timer == 60) { render(app, &v); save_shot(&v, shots, "10_stats"); app_key(app, ' '); }
                if (app->state == ST_CORNER && app->fight.round == 2) { app_tick(app); render(app, &v); save_shot(&v, shots, "10b_corner"); app_key(app, 'S'); }
                else if (app->state == ST_CORNER) app_key(app, ' ');
                if (app->state == ST_RESULT && app->timer == 40) { render(app, &v); save_shot(&v, shots, "11_result"); }
            }
            if (name) { render(app, &v); save_shot(&v, shots, name); }
        }
        app_free(app);
        return 0;
    }

    Uint64 last = SDL_GetTicks(), acc = 0;
    while (app->state != ST_QUIT) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) app->state = ST_QUIT;
            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                int down = e.type == SDL_EVENT_KEY_DOWN;
                if (app->state == ST_FIGHT) {
                    for (size_t i = 0; i < sizeof action_keys / sizeof action_keys[0]; i++)
                        if (action_keys[i].key == e.key.key)
                            app_action_key(app, action_keys[i].who, action_keys[i].action, down);
                }
                if (!down || e.key.repeat) continue;
                if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
                    SDL_SetWindowFullscreen(v.window, !(SDL_GetWindowFlags(v.window) & SDL_WINDOW_FULLSCREEN));
                    continue;
                }
                int k = 0;
                switch (e.key.key) {
                case SDLK_RETURN: case SDLK_KP_ENTER: k = KEY_ENTER; break;
                case SDLK_ESCAPE: k = KEY_ESC; break;
                case SDLK_BACKSPACE: k = KEY_BACKSPACE; break;
                default:
                    if (e.key.key >= 32 && e.key.key < 127) {
                        k = (int)e.key.key;
                        if ((e.key.mod & SDL_KMOD_SHIFT) && k >= 'a' && k <= 'z') k -= 32;
                    } else if (e.key.key >= SDLK_KP_1 && e.key.key <= SDLK_KP_9 && app->state != ST_FIGHT)
                        k = '1' + (int)(e.key.key - SDLK_KP_1);
                    else if (e.key.key == SDLK_KP_0 && app->state != ST_FIGHT) k = '0';
                }
                if (k) { app_key(app, k); play_sounds(app); }
            }
        }
        Uint64 now = SDL_GetTicks();
        acc += now - last;
        last = now;
        if (acc > 1000) acc = 1000;
        while (acc >= 100) { app_tick(app); play_sounds(app); acc -= 100; }   /* 10 ticks per second */
        render(app, &v);
        SDL_RenderPresent(v.renderer);
    }
    app_free(app);
    SDL_free(app);
    audio_quit();
    SDL_DestroyTexture(v.frame);
    SDL_DestroyTexture(v.target);
    SDL_DestroyRenderer(v.renderer);
    SDL_DestroyWindow(v.window);
    SDL_Quit();
    return 0;
}

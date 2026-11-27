#include "assets.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Open dir/name trying the name as given, upper case and lower case. */
static FILE *open_any_case(const char *dir, const char *name)
{
    char path[1200], n[64];
    for (int v = 0; v < 3; v++) {
        snprintf(n, sizeof n, "%s", name);
        if (v)
            for (char *p = n; *p; p++)
                *p = (char)(v == 1 ? toupper((unsigned char)*p) : tolower((unsigned char)*p));
        snprintf(path, sizeof path, "%s/%s", dir, n);
        FILE *f = fopen(path, "rb");
        if (f) return f;
    }
    return NULL;
}

uint8_t *assets_read_file(const char *dir, const char *name, size_t *len)
{
    FILE *f = open_any_case(dir, name);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf && len) *len = (size_t)n;
    return buf;
}

static int has_file(const char *dir, const char *name)
{
    FILE *f = open_any_case(dir, name);
    if (!f) return 0;
    fclose(f);
    return 1;
}

int assets_layout(const char *dir)
{
    if (has_file(dir, "KO.EXE") && has_file(dir, "RING.PIX")) return LAYOUT_SIERRACH;
    if (has_file(dir, "BOXING.PIC") || has_file(dir, "BOXING.PI3")) return LAYOUT_1987;
    return LAYOUT_NONE;
}

/* SierraCh names for the 1987 release's files (docs/ANALYSIS.md section 0). */
static const struct { const char *ext, *name; size_t skip; } sierrach_names[] = {
    {"PIC", "OSIDE.PIX", 0}, {"PI2", "GYM.PIX", 0}, {"PI3", "RING.PIX", 0},
    {"PI4", "STAT.PIX", 0}, {"PI5", "OPTION.PIX", 0}, {"GRL", "GIRL.SPT", 0},
    {"FG3", "FIGHT.OLY", 22224},  /* the fight extras sit at the end of the overlay */
    {"OV1", "KO.CNF", 0}, {"NAM", "NAMES.DAT", 0},
    {"MAIN", "KO.EXE", 0}, {"FIGHT", "FIGHT.OLY", 0},
    {"MS2", "INTRO.OLY", 0},      /* the fight-setup texts (and the intro tune) */
};

static uint8_t *concat3(const char *dir, const char *a, const char *b, const char *c, size_t *len)
{
    size_t la = 0, lb = 0, lc = 0;
    uint8_t *x = assets_read_file(dir, a, &la), *y = assets_read_file(dir, b, &lb),
            *z = assets_read_file(dir, c, &lc), *out = NULL;
    if (x && y && z && (out = malloc(la + lb + lc))) {
        memcpy(out, x, la); memcpy(out + la, y, lb); memcpy(out + la + lb, z, lc);
        *len = la + lb + lc;
    }
    free(x); free(y); free(z);
    return out;
}

uint8_t *assets_read(const char *dir, const char *ext, size_t *len)
{
    size_t dummy;
    if (!len) len = &dummy;
    if (assets_layout(dir) == LAYOUT_SIERRACH) {
        /* xR = xBXR0.SPT (left boxer), xB = xBXR1.SPT (right boxer) */
        if (strlen(ext) == 2 && strchr("BWCK", ext[0]) && (ext[1] == 'R' || ext[1] == 'B')) {
            char name[16];
            snprintf(name, sizeof name, "%cBXR%c.SPT", ext[0], ext[1] == 'R' ? '0' : '1');
            return assets_read_file(dir, name, len);
        }
        for (size_t i = 0; i < sizeof sierrach_names / sizeof sierrach_names[0]; i++) {
            if (strcmp(ext, sierrach_names[i].ext)) continue;
            uint8_t *d = assets_read_file(dir, sierrach_names[i].name, len);
            size_t k = sierrach_names[i].skip;
            if (d && k) {
                if (*len <= k) { free(d); return NULL; }
                memmove(d, d + k, *len - k);
                *len -= k;
            }
            return d;
        }
        return NULL;
    }
    if (!strcmp(ext, "MAIN")) return assets_read_file(dir, "BOXING.OVR", len);
    /* the booter reads the fight overlay as three files in a row */
    if (!strcmp(ext, "FIGHT")) return concat3(dir, "BOXING.OV4", "BOXING.FG2", "BOXING.FG3", len);
    char name[32];
    snprintf(name, sizeof name, "BOXING.%s", ext);
    return assets_read_file(dir, name, len);
}

static int cmp_str(const void *a, const void *b)
{
    return SDL_strcmp(*(char *const *)a, *(char *const *)b);
}

int assets_find_dir(const char *hint, char *out, size_t outlen)
{
    char cand[12][1024];
    int n = 0;
    if (hint && *hint) snprintf(cand[n++], 1024, "%s", hint);
    const char *env = SDL_getenv("CB_GAME_DIR");
    if (env && *env) snprintf(cand[n++], 1024, "%s", env);
    snprintf(cand[n++], 1024, ".");
    snprintf(cand[n++], 1024, "Original_IBM_CGA");
    snprintf(cand[n++], 1024, "../Original_IBM_CGA");
    snprintf(cand[n++], 1024, "../../Original_IBM_CGA");
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(cand[n++], 1024, "%sOriginal_IBM_CGA", base);
        snprintf(cand[n++], 1024, "%s../Original_IBM_CGA", base);
        snprintf(cand[n++], 1024, "%s../../Original_IBM_CGA", base);   /* Remake/build/ in the repo */
    }
    /* Each candidate may be a game folder itself, or hold release folders
     * (any names, e.g. SierraCh/, ArchiveOrg/, 1987_Crack/). A folder is
     * recognised by its files, and SierraCh is preferred: it is complete. */
    char best[1100] = "";
    int best_layout = LAYOUT_NONE;
    for (int i = 0; i < n && best_layout != LAYOUT_SIERRACH; i++) {
        int l = assets_layout(cand[i]);
        if (l > best_layout) { best_layout = l; snprintf(best, sizeof best, "%s", cand[i]); }
        if (l == LAYOUT_SIERRACH) break;
        int count = 0;
        char **subs = SDL_GlobDirectory(cand[i], "*", 0, &count);
        if (!subs) continue;
        /* sorted so the choice does not depend on the file system's order */
        SDL_qsort(subs, (size_t)count, sizeof *subs, cmp_str);
        for (int k = 0; k < count; k++) {
            char path[1100];
            snprintf(path, sizeof path, "%s/%s", cand[i], subs[k]);
            int ls = assets_layout(path);
            if (ls > best_layout) { best_layout = ls; snprintf(best, sizeof best, "%s", path); }
            if (ls == LAYOUT_SIERRACH) break;
        }
        SDL_free(subs);
        /* a nearer candidate (e.g. --game-dir) wins over the later fallbacks */
        if (best_layout != LAYOUT_NONE) break;
    }
    if (best_layout != LAYOUT_NONE) { snprintf(out, outlen, "%s", best); return 1; }
    return 0;
}

void options_defaults(Options *o)
{
    /* What the original uses when it cannot read its options sector (OVR 35FE). */
    o->sound = 1; o->colours = 1; o->human_side = 0; o->players = 2;
    o->rounds = 12; o->scoring = 0; o->round_secs = 180; o->bell = 1;
    o->intro = 1; o->mode = 1; o->centre = 45;
}

static void note_missing(Assets *a, const char *ext)
{
    char label[24];
    snprintf(label, sizeof label, "BOXING.%s", ext);
    if (a->layout == LAYOUT_SIERRACH) {
        if (strlen(ext) == 2 && (ext[1] == 'R' || ext[1] == 'B'))
            snprintf(label, sizeof label, "%cBXR%c.SPT", ext[0], ext[1] == 'R' ? '0' : '1');
        else
            for (size_t i = 0; i < sizeof sierrach_names / sizeof sierrach_names[0]; i++)
                if (!strcmp(ext, sierrach_names[i].ext)) snprintf(label, sizeof label, "%s", sierrach_names[i].name);
    }
    size_t l = strlen(a->missing);
    snprintf(a->missing + l, sizeof a->missing - l, "%s%s", l ? ", " : "", label);
}

static void load_boxer_file(Assets *a, int ch, int side, const uint8_t *d, size_t len)
{
    BoxerSprites *bs = &a->boxers[ch][side];
    size_t p = 0;
    int phase = 0; /* 0 body, 1 head, 2 arms */
    while (p + 6 <= len) {
        int w = d[p] | d[p + 1] << 8, h = d[p + 2] | d[p + 3] << 8;
        if (w <= 0 || w > 40 || h <= 0 || h > 60) break;
        if (phase == 0 && w == 6 && h == 20) phase = 1;
        if (phase == 1 && !(w == 6 && h == 20)) phase = 2;
        Sprite s;
        size_t np = cga_decode_sprite(d, len, p, phase == 0 ? SPR_OPAQUE : phase == 1 ? SPR_HEAD : SPR_MASKED, &s);
        if (!np) break;
        if (phase == 0 && bs->nbody < MAX_FRAMES) bs->body[bs->nbody++] = s;
        else if (phase == 1 && bs->nhead < MAX_FRAMES) bs->head[bs->nhead++] = s;
        else if (phase == 2 && bs->narms < MAX_FRAMES) bs->arms[bs->narms++] = s;
        else sprite_free(&s);
        p = np;
    }
    bs->loaded = bs->nbody > 0 && bs->nhead > 0;
}

static void mirror_set(BoxerSprites *dst, const BoxerSprites *src)
{
    memset(dst, 0, sizeof *dst);
    dst->nbody = src->nbody; dst->nhead = src->nhead; dst->narms = src->narms;
    for (int i = 0; i < src->nbody; i++) sprite_mirror(&src->body[i], &dst->body[i]);
    for (int i = 0; i < src->nhead; i++) sprite_mirror(&src->head[i], &dst->head[i]);
    for (int i = 0; i < src->narms; i++) sprite_mirror(&src->arms[i], &dst->arms[i]);
    dst->loaded = src->loaded;
    dst->mirrored = 1;
}

static int load_sprite_list(const uint8_t *d, size_t len, Sprite *out, int max)
{
    int n = 0;
    size_t p = 0;
    while (n < max) {
        size_t np = cga_decode_sprite(d, len, p, SPR_OPAQUE, &out[n]);
        if (!np) break;
        n++;
        p = np;
    }
    return n;
}

int assets_load(Assets *a, const char *dir)
{
    static const char *scr[SCR_COUNT] = {"PIC", "PI2", "PI3", "PI4", "PI5"};
    static const char chars[] = "BWCK";
    memset(a, 0, sizeof *a);
    snprintf(a->dir, sizeof a->dir, "%s", dir);
    a->layout = assets_layout(dir);
    size_t len;
    uint8_t *d;

    for (int i = 0; i < SCR_COUNT; i++) {
        if ((d = assets_read(dir, scr[i], &len))) {
            cga_decode_screen(d, len, &a->screens[i].fb);
            a->screens[i].loaded = 1;
            free(d);
        } else note_missing(a, scr[i]);
    }

    for (int c = 0; c < CHAR_COUNT; c++)
        for (int s = 0; s < 2; s++) {
            char ext[3] = {chars[c], s ? 'B' : 'R', 0};  /* xR = left corner, xB = right */
            if ((d = assets_read(dir, ext, &len))) {
                load_boxer_file(a, c, s, d, len);
                free(d);
            } else note_missing(a, ext);
        }
    /* A missing corner (e.g. BOXING.WR in some copies) is rebuilt by
     * mirroring the other corner's sprites. These lists are only used for
     * the self-test; the fight is drawn by anim.c from the raw files. */
    for (int c = 0; c < CHAR_COUNT; c++)
        for (int s = 0; s < 2; s++)
            if (!a->boxers[c][s].loaded && a->boxers[c][!s].loaded)
                mirror_set(&a->boxers[c][s], &a->boxers[c][!s]);

    if ((d = assets_read(dir, "GRL", &len))) { a->ngrl = load_sprite_list(d, len, a->grl, 24); free(d); }
    else note_missing(a, "GRL");
    if ((d = assets_read(dir, "FG3", &len))) { a->nfg3 = load_sprite_list(d, len, a->fg3, 32); free(d); }
    else note_missing(a, "FG3");

    /* Roster: names (9 bytes each, slot 0 unused). The 1987 release keeps one
     * 512-byte DAT sector per boxer; SierraCh one 42-byte <NAME>.DAT file each
     * (space -> '_', '.' -> '%'), with the same 21 words. */
    uint8_t *nam = assets_read(dir, "NAM", &len);
    size_t namlen = nam ? len : 0;
    uint8_t *dat = NULL;
    size_t datlen = 0;
    if (!nam) note_missing(a, "NAM");
    if (a->layout != LAYOUT_SIERRACH) {
        dat = assets_read(dir, "DAT", &len);
        datlen = dat ? len : 0;
        if (!dat) note_missing(a, "DAT");
    }
    for (int i = 0; i < MAX_BOXERS && nam && (size_t)(i + 2) * 9 <= namlen; i++) {
        BoxerRecord *r = &a->roster[i];
        uint8_t sector[512] = {0};
        char name[10];
        memcpy(name, nam + (i + 1) * 9, 9);
        name[9] = 0;
        /* GYM 257D lists every name in the name file, whether or not the
         * boxer has a record (SierraCh's HART has no HART.DAT, the 1987
         * SHARKEY an empty sector): such a slot is shown but can't fight. */
        memcpy(r->name, name, 10);
        if (name[0]) a->nboxers = i + 1;
        if (a->layout == LAYOUT_SIERRACH) {
            char fn[16];
            size_t k = 0;
            for (; name[k] && k < 8; k++) fn[k] = name[k] == ' ' ? '_' : name[k] == '.' ? '%' : name[k];
            snprintf(fn + k, sizeof fn - k, ".DAT");
            size_t l = 0;
            uint8_t *d = name[0] ? assets_read_file(dir, fn, &l) : NULL;
            if (!d) continue;
            memcpy(sector, d, l < sizeof sector ? l : sizeof sector);
            free(d);
        } else {
            if (!dat || (size_t)(i + 1) * 512 > datlen) break;
            memcpy(sector, dat + i * 512, 512);
        }
        boxer_parse(sector, r);
        if (!r->name[0]) r->used = 0;
    }
    free(nam);
    free(dat);

    options_defaults(&a->options);
    if ((d = assets_read(dir, "OV1", &len))) {
        if (len >= 21) {   /* OV1 is a 512-byte sector; SierraCh KO.CNF is exactly 21 bytes */
            int v[10];
            for (int i = 0; i < 10; i++) v[i] = (short)(d[i * 2] | d[i * 2 + 1] << 8);
            Options *o = &a->options;
            o->sound = v[0]; o->colours = v[1]; o->human_side = v[2]; o->players = v[3];
            o->rounds = v[4]; o->scoring = v[5]; o->round_secs = v[6] == 60 ? 60 : 180;
            o->bell = v[7]; o->intro = v[8]; o->mode = v[9]; o->centre = d[20];
            if (o->rounds < 1 || o->rounds > 15) o->rounds = 12;
            a->have_options_file = 1;
        }
        free(d);
    }

    /* Essential: the arena screen, at least one boxer set and the roster. */
    int any_boxer = 0;
    for (int c = 0; c < CHAR_COUNT; c++) any_boxer |= a->boxers[c][0].loaded;
    return a->screens[SCR_ARENA].loaded && any_boxer && a->nboxers > 0;
}

void assets_free(Assets *a)
{
    for (int c = 0; c < CHAR_COUNT; c++)
        for (int s = 0; s < 2; s++) {
            BoxerSprites *b = &a->boxers[c][s];
            for (int i = 0; i < b->nbody; i++) sprite_free(&b->body[i]);
            for (int i = 0; i < b->nhead; i++) sprite_free(&b->head[i]);
            for (int i = 0; i < b->narms; i++) sprite_free(&b->arms[i]);
        }
    for (int i = 0; i < a->ngrl; i++) sprite_free(&a->grl[i]);
    for (int i = 0; i < a->nfg3; i++) sprite_free(&a->fg3[i]);
}

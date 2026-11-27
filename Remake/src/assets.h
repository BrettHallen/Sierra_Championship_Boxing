/* Loading the player's own copy of the game files. Nothing here is shipped
 * with the engine; everything comes from BOXING.* in the game folder. */
#ifndef CB_ASSETS_H
#define CB_ASSETS_H

#include "cga.h"
#include "boxer.h"

enum { SCR_TITLE, SCR_GYM, SCR_ARENA, SCR_STATS, SCR_OPTIONS, SCR_COUNT };
enum { CHAR_BLACK, CHAR_WHITE, CHAR_CARTOON, CHAR_KANGAROO, CHAR_COUNT };
enum { SIDE_LEFT, SIDE_RIGHT };
/* Which release the game folder holds (docs/ANALYSIS.md section 0). */
enum { LAYOUT_NONE, LAYOUT_1987, LAYOUT_SIERRACH };

#define MAX_FRAMES 16
#define MAX_BOXERS 60

typedef struct {
    int loaded;        /* 1 if the file was found */
    int mirrored;      /* 1 if built by mirroring the other corner's file */
    int nbody, nhead, narms;
    Sprite body[MAX_FRAMES];  /* legs and whole-body poses (knock-down, KO) */
    Sprite head[MAX_FRAMES];  /* head + upper body */
    Sprite arms[MAX_FRAMES];  /* masked arms / punches */
} BoxerSprites;

typedef struct {
    int loaded;
    Framebuffer fb;
} Screen;

/* Options as stored in BOXING.OV1 (see docs/GAMEPLAY.md section 2). */
typedef struct {
    int sound, colours, human_side, players, rounds, scoring;
    int round_secs, bell, intro, mode, centre;
} Options;

typedef struct {
    char dir[1024];
    int layout;                /* LAYOUT_1987 or LAYOUT_SIERRACH */
    Screen screens[SCR_COUNT];
    BoxerSprites boxers[CHAR_COUNT][2];
    int ngrl, nfg3;
    Sprite grl[24];
    Sprite fg3[32];
    int nboxers;               /* slots in use (1..59) */
    BoxerRecord roster[MAX_BOXERS];
    Options options;
    int have_options_file;
    char missing[512];         /* human-readable list of files not found */
} Assets;

/* Find a folder holding the game files: the given path, $CB_GAME_DIR, or a few
 * likely places next to the executable / current directory. */
int assets_find_dir(const char *hint, char *out, size_t outlen);
/* Load everything we can. Returns 0 if the essential files are missing. */
int assets_load(Assets *a, const char *dir);
void assets_free(Assets *a);
void options_defaults(Options *o);

/* Read a game file by its 1987 name (the extension after "BOXING.", any letter
 * case). In a SierraCh folder the name is mapped to that release's file, e.g.
 * PI3 -> RING.PIX, BR -> BBXR0.SPT. Two extra names: "MAIN" is the main
 * program (BOXING.OVR / KO.EXE) and "FIGHT" the whole fight overlay
 * (OV4+FG2+FG3 / FIGHT.OLY). Caller frees. */
uint8_t *assets_read(const char *dir, const char *ext, size_t *len);
/* Read dir/name exactly (any letter case). Caller frees. */
uint8_t *assets_read_file(const char *dir, const char *name, size_t *len);
int assets_layout(const char *dir);

#endif

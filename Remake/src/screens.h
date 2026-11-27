/* Game screens and their flow. Platform-independent: main.c feeds keys in and
 * draws the framebuffer plus the 40x25 text layer. */
#ifndef CB_SCREENS_H
#define CB_SCREENS_H

#include "assets.h"
#include "fight.h"
#include "anim.h"

#define TEXT_COLS 40
#define TEXT_ROWS 25

typedef enum {
    ST_ERROR, ST_TITLE, ST_EXIT_CONFIRM, ST_GYM_MENU, ST_GYM, ST_GYM_VIEW, ST_OPTIONS,
    ST_GIRL, ST_PICK, ST_TAPE, ST_FIGHT, ST_STATS, ST_CORNER, ST_RESULT, ST_QUIT
} State;

/* Text colours beyond the CGA palette (0-3): the SierraCh release draws its
 * menu screens in text mode, with grey normal text and bright headings. */
enum { TXT_RED = 0x14, TXT_GREY = 0x17, TXT_BRIGHT = 0x1F };

/* Special key codes (anything else is the ASCII character). */
enum { KEY_ENTER = 13, KEY_ESC = 27, KEY_BACKSPACE = 8,
       KEY_LEFT = 256, KEY_RIGHT, KEY_UP, KEY_DOWN };

typedef struct {
    Assets assets;
    Framebuffer fb;
    char text[TEXT_ROWS][TEXT_COLS + 1];
    uint8_t text_colour[TEXT_ROWS][TEXT_COLS];
    int palette;           /* CGA palette 0 or 1 */
    State state;
    int timer;             /* ticks in the current state */
    char input[16];        /* number being typed */
    int view_slot;
    int pick[2];           /* chosen boxer slots, -1 = not yet */
    int picking;           /* 0 left, 1 right */
    int corner_who;        /* whose corner is shown */
    char corner_lines[8][48];
    char corner_keys[8];
    int corner_n;
    int a_variant[2];
    Fight fight;
    uint8_t *rng_code; size_t rng_len;
    int held[ACT_COUNT][2];/* arcade: which action keys are down */
    char status[48];
    /* text placed by pixel (the scoreboard: FG2 130B prints past column 40) */
    struct { int x, y; uint8_t colour; char s[16]; } ptext[8];
    int nptext;
    /* Tale of the Tape (FGT 06DA): the two records as edited there */
    BoxerRecord tape_rec[2];
    int tape_edit;         /* item being typed (1-13), 0 none */
    char tape_msg[48];
    int corner_help;       /* arcade corner: showing the keys */
    int sfx[16], nsfx;     /* sounds for the front end */
    /* the intro (FGT 1CAE): the ring girl and the tune */
    uint16_t tune[128][2]; /* timer divisor (0 rest), length in tenths of a frame */
    int ntune;
    int tune_cmd;          /* for the front end: 1 start the tune, -1 stop it */
    int intro_done;        /* it plays once, the first time Fight is chosen */
    int girl_pos, girl_phase, girl_stage, girl_wait_ms;
    /* the end of the fight (FG2 0C04..12F3, OVR 411F) */
    int res_phase;         /* 0 judges adding up, 1 the ring, 2 the statistics */
    int res_prompt;        /* 0 Another fight, 1 Rematch, 2 Quit or start over, 3 Quit sure */
    int res_units;         /* pause units towards the next cheer step */
    char game_dir[1024];
    Anim anim;             /* the original's boxer drawing (anim.c) */
} App;

void app_init(App *app, const char *game_dir);
void app_free(App *app);
void app_key(App *app, int key);
/* Draw the frame used for the window icon (the ring-girl intro). */
int  app_icon_scene(App *app);
/* Arcade controls: key down/up events, mapped by the caller. */
void app_action_key(App *app, int who, int action, int down);
/* Advance by one tick (10 per second) and redraw fb + text. */
void app_tick(App *app);
void app_set_state(App *app, State s);

/* Text layer helpers */
void txt_clear(App *app);
void txt_at(App *app, int col, int row, uint8_t colour, const char *fmt, ...);
void ptxt_at(App *app, int x, int y, uint8_t colour, const char *fmt, ...);
/* Queue a sound (SFX_*) for the front end. */
void app_sfx(App *app, int sfx);

#endif

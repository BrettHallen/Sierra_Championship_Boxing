/* The fight: rounds, punches, knock-downs, injuries, corners and judges.
 *
 * What is faithful to the original (see docs/GAMEPLAY.md):
 *   - base ratings derived from the boxer record (boxer.c)
 *   - corner advice effects, and their one-round undo
 *   - end-of-round energy loss and recovery
 *   - knock-down side effects, glazed eyes, killer-instinct follow-up
 *   - injury list and referee stoppages
 *   - the three judges' scoring (ten-point must or rounds)
 *   - the random number generator (it reads the user's own BOXING.OVR/FG2)
 *   - the round itself (FG2 16EE..248E): the computer's choices, the hit,
 *     block, stagger, hurt and knock-down rolls, damage and injuries, the
 *     count and getting up, the clock and the bell. It is driven by the
 *     original's animation sequences (anim.c), as the original was. */
#ifndef CB_FIGHT_H
#define CB_FIGHT_H

#include "boxer.h"
#include "assets.h"
#include "anim.h"

enum Action {
    ACT_NONE, ACT_JAB, ACT_UPPERCUT, ACT_HEAD, ACT_BODY,
    ACT_DUCK, ACT_COVER_HEAD, ACT_COVER_BODY, ACT_FORWARD, ACT_BACK, ACT_COUNT
};

enum Injury {
    INJ_NONE, INJ_SLIGHT_CUT, INJ_BAD_GASH, INJ_BRUISED_RIB, INJ_BROKEN_RIB,
    INJ_CUT_NOSE, INJ_BLOODY_NOSE, INJ_SWOLLEN_EYE, INJ_EYE_SHUT, INJ_GLAZED
};

enum Result { RES_NONE, RES_KO, RES_TKO, RES_DECISION, RES_DRAW };

/* Sounds the fight asks for (played by audio.c; see docs/GAMEPLAY.md
 * section 13). */
enum Sfx {
    SFX_NONE, SFX_BELL, SFX_COUNT, SFX_TAP, SFX_BODY, SFX_HEAD, SFX_THUD,
    SFX_BUZZ, SFX_ERROR, SFX_COUNT_
};

typedef struct {
    BoxerRecord rec;
    DerivedBoxer der;
    Ratings live;
    int human;
    int injury;
    int momentum;          /* 83BF */
    int taken;             /* 1D10/1D12: points against him; the judges credit the other man */
    int damage_round;      /* 83FD: taken off his energy at the end of the round */
    int kd_round, kd_total;/* 839F / 83BB */
    int all_out, defensive;/* 83A3 / 83A7 */
    int punches_thrown, punches_landed;   /* remake statistics */
    int down;              /* 1D0C: 0 standing, 1 sitting, 2 flat */
    int down_timer;        /* 0E63: sequence ends before he may move on */
    int style;             /* 1D06: Arcade fighting style, 1 = Slugger */
    int reach_adv;         /* 1DA0: 1 if he has the longer reach */
    int out;               /* lost by knock-out or stoppage */
    int input;             /* arcade key held now (ACT_*) */
    int cmd;               /* arcade key pressed, waiting to be used (ACT_*) */
} FightBoxer;

typedef struct {
    FightBoxer b[2];
    Options opt;
    int round;             /* 1-based */
    int ticks_left;        /* tenths of a second left on the clock (for the display) */
    int between_rounds;
    Anim *anim;            /* the boxers' sequences; the round needs them */
    int turn;              /* 83C7: whose iteration is next */
    int distance;          /* 0E3D: right position - left position */
    int human_in;          /* 0E6E */
    int injured_this_round;/* 0E6F */
    int bell;              /* 1D0A */
    int elapsed;           /* 1D18: clock periods */
    int count_periods;     /* 1D1A */
    int clock_step;        /* 1D1C */
    int clock_face;        /* 1D1E: 17 steps make a round */
    int period;            /* 1D20: iterations left in this clock period */
    int steps_per_face;    /* 8397: 9 for 3-minute rounds, 3 for 1-minute */
    int iter_acc;          /* paces the iterations against the 10 Hz tick */
    int judge_round[3][2];
    int judge_total[3][2];
    int total[2];          /* 83CB/83CD: the three judges' cards added up (rounds won with that scoring) */
    int result, winner;    /* winner: 0 left, 1 right, -1 none */
    int count;             /* 83D5: the referee's count, 0 when none (shown as count - 1) */
    int opening;           /* the introductions before the bell (FG2 27E2 / 2857) */
    int open_step, open_wait, open_n;
    int pending_sfx;       /* 8399: played in place of the next iteration's pause */
    int sfx[8], nsfx;      /* sounds for the front end to play, oldest first */
    char line1[48], line2[48];
    /* random generator state */
    const uint8_t *rng_bytes; size_t rng_len; unsigned rng_idx, rng_acc;
} Fight;

void fight_init(Fight *f, const Assets *a, int left_slot, int right_slot);
void fight_set_rng_source(Fight *f, const uint8_t *code, size_t len, unsigned seed);
/* the original's pause routine counts calls; this many make one frame */
#define UNITS_PER_ITER 1750
/* the front end ticks 10 times a second: 51 iterations every 40 ticks */
#define UNITS_PER_TICK (UNITS_PER_ITER * 51 / 40)
int  fight_random(Fight *f);       /* 0..127 like the original */
/* Advance one tick (the engine runs 10 ticks per second). Returns 1 when the
 * round has ended (check f->result for the end of the fight). Needs f->anim
 * set and loaded (anim_load); without the sequences nothing happens in the
 * ring and the round just runs out. */
int  fight_tick(Fight *f);
/* One iteration of the original's loop (one boxer's frame). 1 = round over. */
int  fight_iterate(Fight *f);
/* An arcade key was pressed for a boxer (ACT_*). */
void fight_press(Fight *f, int who, int action);
void fight_end_round(Fight *f);    /* energy, judges, flags */
void fight_sfx(Fight *f, int sfx);
void fight_start_round(Fight *f);
/* Corner advice keys: Q W E A S D X C (see docs/GAMEPLAY.md section 5). */
int  fight_corner_options(const Fight *f, int who, char lines[8][48], char keys[8]);
void fight_apply_advice(Fight *f, int who, char key, int variant);
char fight_auto_advice(Fight *f, int who);
const char *injury_text(int injury);
const char *energy_text(int energy);

#endif

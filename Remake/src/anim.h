/* Boxer animation, a port of the original fight overlay's draw routine
 * (BOXING.OVR 67A3) and its tables. See docs/GAMEPLAY.md section 11.
 *
 * Nothing is built in: at run time the sprite pointer tables are read from the
 * code that fills them (BOXING.OVR, or FIGHT.OLY in the SierraCh release), the
 * animation sequences from the fight overlay's data (OV4+FG2+FG3 / FIGHT.OLY),
 * and the sprites from the boxer files. They
 * are laid out in a 64K image of the original data segment, exactly where the
 * original put them, and drawn into a 16K CGA video memory image that starts
 * as the arena (BOXING.PI3). */
#ifndef CB_ANIM_H
#define CB_ANIM_H

#include "cga.h"

/* The sequence slots, in the order of the original's table at DS 0E7E (four
 * bytes per slot: left boxer's sequence, then the right boxer's). */
enum {
    SEQ_GUARD_HEAD,   /* 0E7E glove up at the face (defence against head punches) */
    SEQ_DUCK_REACT,   /* 0E82 crouch away from a head punch */
    SEQ_DUCK_COUNTER, /* 0E86 crouch, then a punch from the crouch */
    SEQ_MISS,         /* 0E8A arm left out after a miss */
    SEQ_IDLE,         /* 0E8E standing guard, one frame */
    SEQ_IDLE_LONG,    /* 0E92 the same, three frames */
    SEQ_HURT,         /* 0E96 rocked by a punch */
    SEQ_FALL,         /* 0E9A knocked down */
    SEQ_LYING,        /* 0E9E flat on the canvas */
    SEQ_SITTING,      /* 0EA2 sitting up during the count */
    SEQ_GET_UP,       /* 0EA6 back on his feet */
    SEQ_JAB,          /* 0EAA */
    SEQ_CROUCH,       /* 0EAE short crouch (blocked, or off the ropes) */
    SEQ_BODY_A,       /* 0EB2 body punch */
    SEQ_BODY_B,       /* 0EB6 body punch, other hand */
    SEQ_HOOK,         /* 0EBA head punch with a step in */
    SEQ_UPPERCUT,     /* 0EBE */
    SEQ_REEL,         /* 0EC2 staggering about (state 50h) */
    SEQ_REEL_B,       /* 0EC6 the same, one more frame */
    SEQ_STANCE,       /* 0ECA one frame of stance */
    SEQ_SHUFFLE,      /* 0ECE shuffle forward */
    SEQ_STEP_FWD,     /* 0ED2 */
    SEQ_STEP_FWD2,    /* 0ED6 two steps forward */
    SEQ_STEP_BACK,    /* 0EDA */
    SEQ_STEP_BACK2,   /* 0EDE two steps back (to a neutral corner) */
    SEQ_DUCK,         /* 0EE2 duck, one frame */
    SEQ_COVER,        /* 0EE6 cover up, one frame */
    SEQ_SLOTS,        /* the slot table ends here */
    /* Knock-down falls that are not in the slot table: FG2 1A1F..1A51 loads
     * them directly (1987 DS 09F3/0A26 left, 0C5E/0C96 right). */
    SEQ_FALL_ROPES = SEQ_SLOTS, /* flat on his back, far from the centre */
    SEQ_FALL_SIT,     /* down on the seat, near the centre */
    SEQ_COUNT
};

#define ANIM_START_LEFT  0x0BAA   /* even-bank offsets at the first bell */
#define ANIM_START_RIGHT 0x0BBA

typedef struct {
    int pos;                    /* even-bank byte offset (DS:205F / DS:2061) */
    int arm_pos, arm_w, arm_h;  /* last arm rectangle, erased next frame */
    int body_pos, body_w;       /* last body rectangle */
    unsigned p;                 /* next frame in the current sequence (DS offset) */
    int seq;                    /* current slot, -1 none */
    int state;                  /* state byte of the last frame drawn */
} AnimBoxer;

typedef struct Layout Layout;

typedef struct {
    const Layout *lay;          /* which release's addresses */
    int ok;                     /* tables and sequences found */
    int have_boxers;
    uint8_t *ds;                /* 64K data segment image */
    uint16_t seq[SEQ_COUNT][2];
    uint8_t bg[0x4000], vram[0x4000];
    AnimBoxer b[2];
    int flower_step, flower_pos;  /* DS 1D18 / 1D1A */
    uint16_t clock[17][2];      /* FG2 2573: the clock hand for each step: sprite, screen offset */
    int nclock;
    uint16_t touch[2];          /* FG2 27E2: the frames where they touch gloves (0 = not found) */
    /* The end of a fight (FG2 1165, 2690): the loser's frame after a decision
     * or stoppage [0][side] and after a knock-out [1][side]; the winner's
     * frames, [side][0] arm down (1 time in 6), [side][1] glove raised; the
     * thrown flower: three frames, then the patch that erases it. */
    uint16_t end_pose[2][2], cheer[2][2], flower[4];
    char note[96];              /* e.g. which file stood in for a missing one */
} Anim;

/* Read the tables (OVR, OV4+FG2+FG3) and the arena (PI3). */
int  anim_load(Anim *a, const char *dir);
void anim_free(Anim *a);
/* Put the boxer files in place: character 0..3 = B W C K. The left boxer uses
 * BOXING.<c>R (SierraCh: <c>BXR0.SPT), the right boxer BOXING.<c>B (<c>BXR1.SPT). A missing file is replaced by
 * another character's file for the same corner. Returns 0 if none was found. */
int  anim_set_boxers(Anim *a, const char *dir, int left_char, int right_char);
/* Arena back, both boxers at their start positions, standing. */
void anim_reset(Anim *a);
/* The same, with the boxers at given positions (rounds after the first start
 * from the corners). */
void anim_reset_at(Anim *a, int left_pos, int right_pos);
void anim_play(Anim *a, int side, int seq);
int  anim_busy(const Anim *a, int side);
/* The fight logic's view (FG2 1BD3): has the current sequence reached its end
 * marker? */
int  anim_ended(const Anim *a, int side);
/* Draw one frame of the current sequence and move on (FG2 1FCA). Works
 * without boxer files too: then only the position and state change. Returns
 * the frame's state byte. */
int  anim_frame(Anim *a, int side);
/* Draw the next frame of the side's sequence (idle when none). */
void anim_step(Anim *a, int side);
int  anim_x(const Anim *a, int side);   /* pos as a pixel column */
void anim_to_fb(const Anim *a, Framebuffer *fb);
/* Draw an opaque sprite from the data segment at a screen offset (OVR 6B91). */
void anim_blit(Anim *a, unsigned addr, int off);
/* Draw one frame at a data address and stop there (OVR 67A3 called on its
 * own, as the introductions do). */
void anim_frame_at(Anim *a, int side, unsigned addr);
/* The ring girl's intro (FGT 1CAE): put GIRL.SPT / BOXING.GRL where the
 * original loads it (the left boxer's place), clear the ring, and blit one
 * of its pieces (an offset into that file) at a screen offset. */
int  anim_load_girl(Anim *a, const char *dir);
void anim_girl(Anim *a, unsigned file_off, int screen_off);
/* The ring after the fight (FG2 2690): when the winner is 0 or 1 and a
 * random draw says so, a new frame for the winner; then the flower thrown
 * from the crowd. Call it every 4000 pause units. rnd() returns 0..127. */
void anim_cheer(Anim *a, int winner, int (*rnd)(void *), void *ctx);
void anim_cheer_end(Anim *a);
/* The clock hand at step 1..17 (FG2 2573). */
void anim_clock(Anim *a, int step);

#endif

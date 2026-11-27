#include "fight.h"
#include <stdio.h>
#include <string.h>

#define TICKS_PER_SEC 10
/* The original has no timer in its loop: a round is 17 clock steps of 9 (or
 * 3) periods of 15 iterations, one iteration being one boxer's frame. To make
 * a round last its nominal 3 (or 1) minutes the remake runs 2295 / 180 =
 * 12.75 iterations a second, 51 every 40 ticks. */
#define ITER_NUM 51
#define ITER_DEN 40
#define CLOCK_FACES 17
#define PERIOD 15
/* The original's pauses are busy loops (OVR 62EA) counted in calls of its
 * random number routine, about 45 microseconds each on a 4.77 MHz PC. One
 * iteration of the round (a frame and an 800-call pause) is 1/12.75 s, so
 * about 1750 calls make one iteration. */
static int units(int n) { return (n + UNITS_PER_ITER / 2) / UNITS_PER_ITER; }

void fight_sfx(Fight *f, int sfx)
{
    if (sfx > SFX_NONE && sfx < SFX_COUNT_ && f->nsfx < 8) f->sfx[f->nsfx++] = sfx;
}

/* ------------------------------------------------------------------ random */

void fight_set_rng_source(Fight *f, const uint8_t *code, size_t len, unsigned seed)
{
    f->rng_bytes = code; f->rng_len = len;
    f->rng_idx = 0; f->rng_acc = seed & 0x7F;
}

int fight_random(Fight *f)
{
    /* The original (OVR 6203) adds successive bytes of its own code to a
     * running total and keeps 7 bits. We do the same with the user's copy of
     * the fight overlay when we have it, else fall back to an LCG. */
    if (f->rng_bytes && f->rng_len) {
        f->rng_acc = (f->rng_acc + f->rng_bytes[f->rng_idx]) & 0x7F;
        if (++f->rng_idx >= f->rng_len) f->rng_idx = 0;
        return (int)f->rng_acc;
    }
    f->rng_acc = f->rng_acc * 1103515245u + 12345u;
    return (int)((f->rng_acc >> 16) & 0x7F);
}

/* ------------------------------------------------------------------- texts */

const char *injury_text(int i)
{
    static const char *t[] = {"Not hurt.", "Slight cut over eye", "Bad gash over eye.",
        "Bruised rib.", "Broken rib.", "Cut on nose.", "Bloody nose.", "Swollen eye.",
        "Eye swollen shut.", "Eyes are glazed."};
    return (i >= 0 && i <= 9) ? t[i] : "";
}

const char *energy_text(int e)
{
    if (e > 90) return "Breathing heavy.";
    if (e > 65) return "Noticeably tired.";
    if (e > 35) return "Very tired.";
    return "Exhausted.";
}

/* -------------------------------------------------------------------- setup */

/* FGT 13BC..17D2: what the two men do to each other's numbers. */
static void pair_adjust(Fight *f)
{
    FightBoxer *L = &f->b[0], *R = &f->b[1];
    Ratings *l = &L->der.base, *r = &R->der.base;
    int wl = L->rec.weight, wr = R->rec.weight;
    for (int i = 0; i < 2; i++) {
        Ratings *me = &f->b[i].der.base;
        if (f->opt.round_secs == 60) {      /* 1-minute rounds: more knock-downs, more cuts */
            me->vulnerable *= 4;
            f->b[i].der.injury_threshold -= 1;
        }
        me->vulnerable += 3 * f->b[!i].rec.hitter;   /* FGT 06C4 / 152D */
    }
    if (wl > wr + 25) {                     /* FGT 1556: the left man is heavier */
        int d = wl - wr - 25;
        if (wl > 235) d /= 4;
        if (wl > 280) d = 0;
        r->vulnerable += d;
        if (wl < 235) {
            r->toughness /= d > 20 ? 3 : 2;
            r->aggression -= d;
        }
        r->openness += d;
    }
    if (wr > wl + 25) {                     /* FGT 163F: the right man is heavier */
        int d = wr - wl - 25;
        if (wr > 235) d /= 4;
        if (wr > 280) d = 0;
        l->vulnerable += d;
        if (wr < 235) {
            l->toughness /= d > 20 ? 3 : 2;
            r->aggression -= d;             /* sic: the original lowers 1D76, the heavier man's own */
        }
        l->openness += d;
    }
    L->reach_adv = R->reach_adv = 0;        /* FGT 1728: reach */
    if (L->rec.reach > R->rec.reach + 5) { L->reach_adv = 1; l->head_target += 20; r->head_target -= 20; }
    else if (R->rec.reach > L->rec.reach + 5) { R->reach_adv = 1; r->head_target += 20; l->head_target -= 20; }
}

void fight_init(Fight *f, const Assets *a, int ls, int rs)
{
    const uint8_t *rb = f->rng_bytes; size_t rl = f->rng_len; unsigned ri = f->rng_acc;
    Anim *an = f->anim;
    memset(f, 0, sizeof *f);
    f->rng_bytes = rb; f->rng_len = rl; f->rng_acc = ri;
    f->anim = an;
    f->opt = a->options;
    int slot[2] = {ls, rs};
    for (int i = 0; i < 2; i++) {
        FightBoxer *b = &f->b[i];
        b->rec = a->roster[slot[i]];
        boxer_derive(&b->rec, &b->der);
        b->style = 1;                       /* FGT 12D1: everyone starts as a Slugger */
    }
    pair_adjust(f);
    for (int i = 0; i < 2; i++) f->b[i].live = f->b[i].der.base;  /* OVR 3330: base to live at the first bell */
    /* FGT 12B6: who is human. Only Arcade has live boxers; players 2 = both,
     * 1 = the chosen side. */
    if (f->opt.mode == 2) {
        if (f->opt.players == 2) f->b[0].human = f->b[1].human = 1;
        else if (f->opt.players == 1) f->b[f->opt.human_side == 1 ? 1 : 0].human = 1;   /* 8190: 0 left, 1 right */
    }
    f->steps_per_face = f->opt.round_secs == 60 ? 3 : 9;   /* FGT 1397 */
    f->winner = -1;
    f->round = 1;
    fight_start_round(f);
}

static int pos_of(const Fight *f, int i);
static void back_off(Fight *f, int i);

void fight_start_round(Fight *f)
{
    f->between_rounds = 0;
    /* FG2 0845 */
    f->count = f->count_periods = 0;
    f->clock_face = f->clock_step = f->elapsed = 0;
    f->bell = 0;
    f->period = PERIOD;
    f->iter_acc = 0;
    f->ticks_left = (f->opt.round_secs == 60 ? 60 : 180) * TICKS_PER_SEC;
    for (int i = 0; i < 2; i++) {
        FightBoxer *b = &f->b[i];
        /* FG2 07E1: a slow starter has not warmed up yet */
        if (b->rec.starter * 2 > f->round) b->momentum = 40;
        b->kd_round = 0;
        b->damage_round = 0;
        b->taken = 0;
        b->down = 0; b->down_timer = 0;
        b->cmd = 0;
    }
    int last = f->round == f->opt.rounds;
    /* last round: both go for it (FG2 08DA) */
    if (last && f->round > 1) f->b[0].momentum = f->b[1].momentum = 50;
    /* FG2 16EE */
    f->turn = 0;
    f->distance = 50;
    f->human_in = f->b[0].human || f->b[1].human;
    f->injured_this_round = 0;
    f->pending_sfx = 0;
    f->opening = 1;
    f->open_wait = 0;
    f->open_step = 3;                       /* just the bell */
    if (f->anim && f->anim->ok) {
        /* FG2 086B: round 1 opens with the boxers touching gloves in the
         * middle (27E2), the last round with them walking out to touch
         * gloves (2857), the others from the corners. */
        if (f->round == 1) { anim_reset_at(f->anim, 0x0BAA, 0x0BBA); f->open_step = 0; }
        else if (last) { anim_reset_at(f->anim, 0x0BA6, 0x0BBE); f->open_step = 5; f->open_n = 8; }
        else anim_reset_at(f->anim, 0x0B9B, 0x0BCA);
        f->distance = pos_of(f, 1) - pos_of(f, 0);
    }
    snprintf(f->line1, sizeof f->line1, "ROUND %d", f->round);
    f->line2[0] = 0;
}

/* --------------------------------------------------------------- in a round */

/* Position on the ring floor: the even-bank byte offset the original uses. */
static int pos_of(const Fight *f, int i) { return f->anim->b[i].pos; }
static int state_of(const Fight *f, int i) { return f->anim->b[i].state; }
static void play(Fight *f, int i, int seq) { anim_play(f->anim, i, seq); }

static int is_punch(int seq)
{
    return seq == SEQ_JAB || seq == SEQ_HOOK || seq == SEQ_UPPERCUT || seq == SEQ_BODY_A ||
           seq == SEQ_BODY_B || seq == SEQ_DUCK_COUNTER;
}

/* FG2 2499: back off, or crouch out of the ropes. */
static void back_off(Fight *f, int i)
{
    int p = pos_of(f, i);
    if (i == 0) play(f, 0, p > 0x0BB3 ? SEQ_STEP_BACK2 : p <= 0x0B9C ? SEQ_CROUCH : SEQ_STEP_BACK);
    else play(f, 1, p < 0x0BB3 ? SEQ_STEP_BACK2 : p >= 0x0BC9 ? SEQ_CROUCH : SEQ_STEP_BACK);
}

static void finish(Fight *f, int result, int winner)
{
    f->result = result;
    f->winner = winner;
    f->ticks_left = 0;
}

/* FG2 28CE: an arcade command. */
static void arcade_command(Fight *f, int si, int act)
{
    FightBoxer *me = &f->b[si], *op = &f->b[!si];
    int punch = 0, seq = -1;
    switch (act) {
    case ACT_JAB:        seq = SEQ_JAB; punch = 1; break;                    /* 1 */
    case ACT_HEAD:       seq = SEQ_HOOK; punch = 1; break;                   /* 2 */
    case ACT_UPPERCUT:   seq = fight_random(f) < me->live.jab_bias ? SEQ_JAB : SEQ_UPPERCUT; punch = 1; break; /* 3 */
    case ACT_BODY:       seq = fight_random(f) < 65 ? SEQ_BODY_A : SEQ_BODY_B; punch = 2; break;            /* 4 */
    case ACT_DUCK:       seq = SEQ_DUCK; break;                              /* 5 */
    case ACT_COVER_HEAD: seq = SEQ_COVER; break;                             /* 6 */
    case ACT_COVER_BODY: seq = SEQ_IDLE; break;                              /* 7: guard */
    case ACT_FORWARD:    seq = SEQ_SHUFFLE; break;                           /* 8 */
    case ACT_BACK:       seq = SEQ_STEP_BACK; break;                         /* 9 */
    default: return;
    }
    play(f, si, seq);
    if (!punch) return;
    me->punches_thrown++;
    if (fight_random(f) <= me->der.fatigue) me->damage_round++;
    me->taken++;   /* sic: every punch a human throws counts one against him (FG2 2981) */
    /* FG2 2985: a computer opponent may see it coming */
    int os = state_of(f, !si) & 0xF0;
    if (!op->human && f->distance < 17 && fight_random(f) < op->live.defence && os != 0x40 && os != 0x50)
        play(f, !si, punch == 1 ? (fight_random(f) > 60 ? SEQ_DUCK_REACT : SEQ_GUARD_HEAD) : SEQ_IDLE_LONG);
}

void fight_press(Fight *f, int who, int action)
{
    if (who >= 0 && who < 2 && action > ACT_NONE && action < ACT_COUNT) f->b[who].cmd = action;
}

/* FG2 1ADB: damage and injuries for any punch that reached him. Returns 1 if
 * the referee stopped the fight. */
static int take_damage(Fight *f, int si, int body, int going_down)
{
    FightBoxer *me = &f->b[si], *op = &f->b[!si];
    Ratings *r = &me->live;
    int dmg = me->damage_round + 2 + (body ? 4 : 0);
    if ((fight_random(f) > r->defence + r->energy - op->live.power || going_down) &&
        fight_random(f) > me->der.injury_threshold && !f->injured_this_round && fight_random(f) > 40) {
        f->injured_this_round = 1;
        r->skill -= 5; r->openness += 5; dmg += 20; r->toughness--; r->vulnerable += 2;
        int inj = me->injury;
        if (inj == 2 || inj == 4 || inj == 6 || inj == 8) {
            /* hit again on a bad injury */
            r->toughness -= 4; r->skill -= 10; r->aggression -= 40; r->vulnerable += 10;
            if (fight_random(f) > 65) {
                static const char *why[] = {"", "", "can not see!", "", "is bent over in pain!", "",
                                            "has a hard time breathing!", "", "can not see!"};
                me->damage_round = dmg;
                me->down = 1; me->out = 1;
                snprintf(f->line1, sizeof f->line1, "REFEREE STOPS FIGHT DUE TO INJURY!");
                snprintf(f->line2, sizeof f->line2, "%s %s", me->rec.name, why[inj]);
                finish(f, RES_TKO, !si);
                fight_sfx(f, SFX_BUZZ);   /* FG2 0C5E */
                return 1;
            }
        } else if (inj == 1 || inj == 3 || inj == 5 || inj == 7) me->injury = inj + 1;
        else if (!body) me->injury = fight_random(f) > 80 ? INJ_SLIGHT_CUT : f->rng_acc > 40 ? INJ_CUT_NOSE : INJ_SWOLLEN_EYE;
        else me->injury = INJ_BRUISED_RIB;
    }
    me->damage_round = dmg;
    return 0;
}

/* FG2 1969: knocked down. */
static void knock_down(Fight *f, int si)
{
    FightBoxer *me = &f->b[si], *op = &f->b[!si];
    Ratings *r = &me->live;
    int bp = pos_of(f, si);
    me->kd_total++; me->damage_round += 75; r->aggression -= 10; me->kd_round++;
    me->taken += 130; r->toughness -= 3; r->vulnerable += 2; r->openness += 2;
    if (fight_random(f) < op->der.killer_chance) op->momentum = 60;
    if (fight_random(f) - 30 > r->energy && me->injury == INJ_NONE) {
        me->injury = INJ_GLAZED;
        r->aggression = 10; r->openness = 65; r->vulnerable += 10;
    }
    me->momentum = -40;
    me->down_timer = 16;
    /* the other man steps away unless he is on the ropes already */
    if (si == 0 && pos_of(f, 1) < 0x0BC9) play(f, 1, SEQ_STEP_BACK);
    if (si == 1 && pos_of(f, 0) > 0x0B9C) play(f, 0, SEQ_STEP_BACK);
    /* he falls flat when he is pushed back past the centre, else sits down */
    if ((si == 0 && bp < 0x0BB3) || (si == 1 && bp > 0x0BB3)) {
        int far = si == 0 ? bp < 0x0BA0 : bp > 0x0BC5;
        play(f, si, far ? SEQ_FALL_ROPES : SEQ_FALL);
        me->down = 2;
    } else {
        play(f, si, SEQ_FALL_SIT);
        me->down = 1;
    }
    snprintf(f->line1, sizeof f->line1, "%s IS DOWN!", me->rec.name);
}

/* FG2 1779..17DB: is a punch of the other man's landing on him? Then
 * 1815..1BD3: what it does. Returns 1 if the fight is over. */
static int contact(Fight *f, int si, int own_hi)
{
    FightBoxer *me = &f->b[si], *op = &f->b[!si];
    int bl = state_of(f, !si) & 0x0F;   /* 2: head punch landing, 5: body punch landing */
    int bp = pos_of(f, si);
    if (bl == 2 && (own_hi == 0x10 || own_hi == 0x30) &&
        (fight_random(f) < op->live.skill || f->human_in)) ;
    else if (bl == 5 && (own_hi == 0x20 || own_hi == 0x30)) ;
    else return 0;
    int body = bl == 5;
    int thr = f->b[si].momentum + me->live.aggression;
    if (thr < 15) thr = 15;
    me->taken++;
    if (f->human_in) me->taken += 3;
    f->pending_sfx = body ? SFX_BODY : SFX_HEAD;   /* FG2 1833: 2 body, 3 head */
    if (f->human_in) {
        /* Arcade: a quicker style can slip it */
        if (me->style < op->style || !me->human) {
            int slip = fight_random(f) > 50;
            if (!slip && me->style + 1 < op->style) slip = fight_random(f) > 50;
            if (slip) { me->taken--; return 0; }
        }
    }
    if (fight_random(f) > thr) {
        /* FG2 1879: he was ready for it */
        if (bp == 0x0B9B || bp == 0x0BCA) {
            if (body || fight_random(f) < 100) play(f, si, SEQ_IDLE);
            else { play(f, si, SEQ_REEL); me->taken += 3; }
        } else if (bp == 0x0B9C || bp == 0x0BC9) {
            if (fight_random(f) < 100) goto guard;
            play(f, si, SEQ_REEL_B); me->taken += 3;
        } else {
            if ((fight_random(f) <= 40 && !body) || me->human) goto guard;
            back_off(f, si);
        }
        return take_damage(f, si, body, 0);
    }
    /* FG2 1910: open to it */
    if (fight_random(f) >= me->live.openness) goto guard;
    {
        int st = f->b[0].style > f->b[1].style ? f->b[0].style : f->b[1].style;
        int r = fight_random(f) + 8 * (st - 1);
        if (r < 85) {                  /* a glancing blow */
            op->punches_landed++;
            play(f, si, body ? SEQ_IDLE : SEQ_MISS);
            return take_damage(f, si, body, 0);
        }
    }
    op->punches_landed++;
    if (fight_random(f) < me->live.vulnerable ||
        ((state_of(f, si) & 0x0F) == 7 && fight_random(f) < 80)) {
        knock_down(f, si);
        return take_damage(f, si, body, 1);
    }
    /* FG2 1A5E: hurt? only away from the ropes */
    if (bp > 0x0B9D && bp < 0x0BC8 && fight_random(f) > 2 * me->live.energy - op->live.power) {
        Ratings *r = &me->live;
        r->aggression -= 2; me->momentum -= 40; r->toughness--; r->vulnerable++;
        me->taken += 8; r->openness++;
        if (fight_random(f) > r->energy) me->momentum -= 40;
        if (fight_random(f) < op->der.killer_chance) op->momentum = 60;
        play(f, si, SEQ_HURT);
    } else if (body) play(f, si, SEQ_CROUCH);
    else if (me->human) goto guard;
    else play(f, si, SEQ_MISS);
    return take_damage(f, si, body, 0);
guard:
    /* FG2 18C1 */
    if (me->human) play(f, si, SEQ_STANCE);
    else play(f, si, body ? SEQ_IDLE : SEQ_GUARD_HEAD);
    return take_damage(f, si, body, 0);
}

/* FG2 1BF7: on the canvas, at the end of each of his sequences. */
static void on_the_canvas(Fight *f, int si)
{
    FightBoxer *me = &f->b[si];
    int cx = me->down_timer - 1, down = me->down, seq;
    int can = me->live.toughness > 0 &&
              (fight_random(f) < me->live.toughness || (f->bell && f->round == f->opt.rounds)) &&
              me->kd_round < 3 && f->count > 1 && cx <= 0;
    if (can) {
        if (down == 2) { seq = SEQ_SITTING; cx = 16; }
        else {
            seq = SEQ_GET_UP; cx = 1;
            if (f->bell == 1) f->elapsed = 1;
            snprintf(f->line1, sizeof f->line1, "%s IS UP!", me->rec.name);
        }
        down--;
    } else {
        if (down == 1 && fight_random(f) > me->live.energy && cx <= 0) { down = 2; cx = 20; }
        seq = down == 2 ? SEQ_LYING : SEQ_SITTING;
    }
    me->down = down;
    me->down_timer = cx;
    play(f, si, seq);
}

/* FG2 1C98..1E6C: the computer's next move (a human's too, before his key
 * is read: with no key he stands). */
static void choose(Fight *f, int si, int thr)
{
    FightBoxer *me = &f->b[si], *op = &f->b[!si];
    int bp = pos_of(f, si), cx = f->distance;
    int o_lo = state_of(f, !si) & 0x0F, o_hi = state_of(f, !si) & 0xF0;
    if (op->down || f->bell || f->count) {
        /* to a neutral corner, or wait */
        if (bp > 0x0B9E && bp < 0x0BC7) play(f, si, SEQ_STEP_BACK2);
        else play(f, si, fight_random(f) > 30 ? SEQ_IDLE_LONG : SEQ_STANCE);
        return;
    }
    if (cx >= me->reach_adv + 16 || me->human || f->count) goto approach;
    if (o_hi == 0x50) { back_off(f, si); return; }
    if (fight_random(f) < thr / 2 || op->human || (o_lo == 7 && fight_random(f) < thr)) {
        /* attack (1D23), unless the bell is about to go */
        if (f->clock_face >= 16 && f->clock_step >= f->steps_per_face - 1) goto defend;
        if (fight_random(f) < me->der.fatigue) me->damage_round++;
        if (fight_random(f) < me->live.head_target) {
            if (cx < 15) { back_off(f, si); return; }
            play(f, si, fight_random(f) > me->live.jab_bias ? SEQ_UPPERCUT : SEQ_JAB);
        } else if (cx < 15) {
            int r = fight_random(f);
            play(f, si, r > 100 ? SEQ_DUCK_COUNTER : r > 60 ? SEQ_BODY_A : r > 30 ? SEQ_BODY_B : SEQ_HOOK);
        } else if (o_hi == 0x50) play(f, si, SEQ_IDLE_LONG);
        else goto close_in;
        return;
    }
defend:
    if (fight_random(f) < me->live.openness) { play(f, si, fight_random(f) > 60 ? SEQ_IDLE : SEQ_IDLE_LONG); return; }
    if (fight_random(f) < me->live.defence) {
        int r = fight_random(f);
        play(f, si, r > 100 ? SEQ_DUCK_REACT : r > 60 ? SEQ_GUARD_HEAD : SEQ_CROUCH);
        return;
    }
    if (fight_random(f) > thr && bp > 0x0B9C && bp < 0x0BC9) { back_off(f, si); return; }
    play(f, si, SEQ_STANCE);
    return;
approach:
    if ((fight_random(f) < thr || o_lo == 7) && !me->human && o_hi != 0x50) goto close_in;
    play(f, si, SEQ_STANCE);
    return;
close_in:
    if (cx > 24) play(f, si, SEQ_STEP_FWD2);
    else play(f, si, fight_random(f) > 40 ? SEQ_SHUFFLE : SEQ_STEP_FWD);
}
/* FG2 2161: every 15 iterations, the clock, the count and the bell. Returns 1
 * when the round (or the fight) is over. */
static int clock_period(Fight *f, int si)
{
    FightBoxer *me = &f->b[si];
    f->period = PERIOD;
    if (++f->clock_step == f->steps_per_face) {
        f->clock_step = 0;
        f->clock_face++;
        if (f->anim) anim_clock(f->anim, f->clock_face);
        /* FG2 25E3: 3-minute rounds skip the 8th step (it draws the same hand) */
        if (f->clock_face == 7 && f->opt.round_secs != 60) f->clock_face++;
    }
    f->elapsed++;
    /* a change of pace (for the man about to move) */
    if (fight_random(f) < 20) {
        if (fight_random(f) < me->live.energy) { me->momentum = 60; me->damage_round += 5; }
        else if (fight_random(f) > 80) { me->momentum = -65; me->damage_round -= 15; }
        else me->momentum = -30;
    }
    int anyone_down = f->b[0].down || f->b[1].down;
    if (anyone_down || f->count) {
        if (!f->count) {
            f->count = 1;
            snprintf(f->line2, sizeof f->line2, "COUNT");
        }
        if (++f->count_periods > 2) {
            /* the count goes to 9 even if he is up, and stops at the bell */
            if ((f->count >= 9 || f->bell) && !anyone_down) {
                f->count = -1;
                f->count_periods = 0;
                snprintf(f->line2, sizeof f->line2, "ROUND %d", f->round);
            }
            if (++f->count == 11) {
                int loser = f->b[0].down ? 0 : 1;
                f->b[loser].out = 1;
                snprintf(f->line1, sizeof f->line1, "%s IS KNOCKED OUT!", f->b[loser].rec.name);
                snprintf(f->line2, sizeof f->line2, "COUNT 10");
                finish(f, RES_KO, !loser);
                fight_sfx(f, SFX_BELL);   /* FG2 22A1 */
                return 1;
            }
            if (f->count) snprintf(f->line2, sizeof f->line2, "COUNT %d", f->count - 1);
            if (f->count > 1) fight_sfx(f, SFX_COUNT);   /* FG2 2227 */
        }
    }
    /* FG2 2408: the bell. Not while a man is down, except in the last round. */
    if (f->clock_face >= CLOCK_FACES && !f->bell &&
        (!anyone_down || f->round == f->opt.rounds)) {
        f->clock_face++;
        f->bell = 1;
        f->elapsed = 0;
        fight_sfx(f, SFX_BELL);           /* FG2 2431 */
        snprintf(f->line1, sizeof f->line1, "END OF ROUND %d", f->round);
    }
    return f->bell && f->elapsed >= 5 && !f->b[0].down && !f->b[1].down;
}

int fight_iterate(Fight *f)
{
    if (f->result) return 1;
    Anim *an = f->anim;
    if (!an || !an->ok) {
        /* nothing to drive the boxers: just run the clock */
        if (--f->period > 0) return 0;
        return clock_period(f, f->turn);
    }
    int si = f->turn;
    FightBoxer *me = &f->b[si];
    int bp = pos_of(f, si);
    int thr = me->momentum + me->live.aggression;      /* 83FB */
    if (thr < 15) thr = 15;
    int own_hi = state_of(f, si) & 0xF0;

    /* 17C2: close enough for a punch to land? */
    if (f->distance < 16 + f->human_in && own_hi != 0x40 && own_hi != 0x50)
        if (contact(f, si, own_hi)) return 1;

    /* 1BD3: his sequence has run out: what next? */
    if (anim_ended(an, si)) {
        if (me->down) on_the_canvas(f, si);
        else {
            choose(f, si, thr);
            /* 1E70: a human's key */
            if (me->human && !f->b[0].down && !f->b[1].down && !f->count && !f->bell) {
                int act = me->cmd ? me->cmd : me->input;
                me->cmd = 0;
                if (act) arcade_command(f, si, act);
            } else if (!me->human && is_punch(an->b[si].seq)) me->punches_thrown++;
        }
        /* 1F4F: too close, or on the ropes (the man on the canvas is in state 40h) */
        int hi = own_hi;
        if (f->distance < 13 && hi != 0x40 && hi != 0x50 &&
            ((si == 0 && bp > 0x0B9E) || (si == 1 && bp < 0x0BC7)))
            back_off(f, si);
        if (((si == 0 && bp <= 0x0B9B) || (si == 1 && bp >= 0x0BCA)) && hi != 0x50 && hi != 0x40) {
            if (f->distance < 17 && fight_random(f) > 70 && !me->human) play(f, si, SEQ_CROUCH);
            else play(f, si, SEQ_SHUFFLE);
        }
    }

    /* 1FCA: draw his frame */
    int st = anim_frame(an, si);
    /* 2031: a man hitting the canvas; 203E: the sound replaces the pause */
    if (st == 0x48) f->pending_sfx = SFX_THUD;
    if (f->pending_sfx) { fight_sfx(f, f->pending_sfx); f->pending_sfx = 0; }
    f->distance = pos_of(f, 1) - pos_of(f, 0);
    f->turn = !si;

    /* 2161 */
    if (--f->period > 0) return 0;
    return clock_period(f, f->turn);
}

/* The introductions, one iteration at a time: FG2 27E2 / 2762 (round 1),
 * 2857 (last round), then the bell (089C). */
static void opening_iterate(Fight *f)
{
    Anim *an = f->anim;
    if (f->open_wait > 0) { f->open_wait--; return; }
    if (!an || !an->ok) f->open_step = 3;
    switch (f->open_step) {
    case 0:   /* standing in the middle */
        anim_frame_at(an, 0, an->seq[SEQ_IDLE_LONG][0]);
        anim_frame_at(an, 1, an->seq[SEQ_IDLE_LONG][1]);
        f->open_wait = units(20000);
        f->open_step = 1;
        break;
    case 1:   /* touching gloves */
        if (an->touch[0] && an->touch[1]) {
            anim_frame_at(an, 0, an->touch[0]);
            anim_frame_at(an, 1, an->touch[1]);
            fight_sfx(f, SFX_TAP);
        }
        f->open_wait = units(15000);
        if (f->round == 1) {
            f->open_step = 2; f->open_n = 70; f->turn = 0;
            anim_play(an, 0, SEQ_IDLE); anim_play(an, 1, SEQ_IDLE);
        } else f->open_step = 3;
        break;
    case 2:   /* 2762: back to their own halves, 35 frames each */
        if (anim_ended(an, f->turn)) back_off(f, f->turn);
        anim_frame(an, f->turn);
        f->turn = !f->turn;
        if (--f->open_n <= 0) { f->open_step = 3; f->open_wait = units(5000); }
        break;
    case 5:   /* 2857: four steps out of the corners each */
        if (f->open_n == 8) { anim_play(an, 0, SEQ_STEP_FWD2); anim_play(an, 1, SEQ_STEP_FWD2); f->turn = 0; }
        anim_frame(an, f->turn);
        f->turn = !f->turn;
        if (--f->open_n <= 0) f->open_step = 1;
        break;
    default:  /* the bell */
        f->opening = 0;
        f->turn = 0;
        if (an && an->ok) {
            an->b[0].seq = an->b[1].seq = -1;
            f->distance = pos_of(f, 1) - pos_of(f, 0);
        }
        fight_sfx(f, SFX_BELL);
        break;
    }
    if (an && an->ok) f->distance = pos_of(f, 1) - pos_of(f, 0);
}

int fight_tick(Fight *f)
{
    if (f->result) return 1;
    int done = 0;
    for (f->iter_acc += ITER_NUM; f->iter_acc >= ITER_DEN && !done; f->iter_acc -= ITER_DEN) {
        if (f->opening) opening_iterate(f);
        else done = fight_iterate(f);
    }
    int total = CLOCK_FACES * f->steps_per_face * PERIOD;
    int gone = (f->clock_face * f->steps_per_face + f->clock_step) * PERIOD + (PERIOD - f->period);
    int secs = f->opt.round_secs == 60 ? 60 : 180;
    f->ticks_left = f->bell ? 0 : (total - gone) * secs * TICKS_PER_SEC / total;
    if (f->ticks_left < 0) f->ticks_left = 0;
    return done || f->result;
}

/* ---------------------------------------------------------- end of a round */

static void judge(Fight *f, int j)
{
    /* FG2 29E8, one judge's card for the round */
    int three_min = f->opt.round_secs != 60;
    int lim = three_min ? 65 : 95;
    /* 1D10/1D12 count against each man: the left man's points come from the
     * right man's counter, and that loop runs first. */
    if (f->b[0].taken < 1) f->b[0].taken = 1;
    if (f->b[1].taken < 1) f->b[1].taken = 1;
    int a = 0, b = 0;
    for (int i = 0; i < f->b[1].taken; i++) if (fight_random(f) < lim) a++;
    for (int i = 0; i < f->b[0].taken; i++) if (fight_random(f) < lim) b++;
    if (fight_random(f) < 40) { if (fight_random(f) < 65) a += 30; else b += 30; }
    while (a < 10 && b < 10) { a++; b++; }
    a *= 10; b *= 10;
    int m = (a > b ? a : b) / 10;
    a /= m; b /= m;
    static const int table[11] = {7, 7, 8, 8, 8, 9, 9, 9, 9, 10, 10};
    a = table[a > 10 ? 10 : a]; b = table[b > 10 ? 10 : b];
    if (f->opt.scoring != 0) { a /= 10; b /= 10; }   /* 8196 non-zero: rounds won (FG2 2ABF) */
    f->judge_round[j][0] = a; f->judge_round[j][1] = b;
    f->judge_total[j][0] += a; f->judge_total[j][1] += b;
    f->total[0] += a; f->total[1] += b;   /* FG2 2AE3 */
}

void fight_end_round(Fight *f)
{
    if (f->result) return;
    int was0 = f->total[0], was1 = f->total[1];
    for (int j = 0; j < 3; j++) judge(f, j);
    if (f->opt.scoring) {
        /* FG2 13E0: with rounds scoring the totals count rounds won */
        int d0 = f->total[0] - was0, d1 = f->total[1] - was1;
        f->total[0] = was0 + (d0 > d1);
        f->total[1] = was1 + (d1 > d0);
    }

    static const int recovery[16] = {0, 15, 15, 15, 15, 15, 15, 10, 10, 10, 10, 10, 6, 6, 6, 6};
    int any_human = f->b[0].human || f->b[1].human;
    for (int i = 0; i < 2; i++) {
        FightBoxer *b = &f->b[i];
        Ratings *base = &b->der.base;
        /* FG2 097F: energy */
        b->live.energy -= b->damage_round / 6;
        if (b->live.energy < base->energy && f->opt.round_secs != 60)
            b->live.energy += recovery[f->round < 16 ? f->round : 15];
        else
            b->live.energy -= 5;
        if (any_human && b->live.energy < base->energy) b->live.energy += 20;
        /* FG2 0A91: corner instructions wear off */
        if (b->all_out) {
            b->live.aggression = base->aggression - 25;
            b->live.openness = base->openness + 5;
            b->live.defence = base->defence - 5;
        }
        if (b->defensive) {
            b->live.aggression = base->aggression;
            b->live.openness = base->openness;
            b->live.defence = base->defence;
        }
        b->all_out = b->defensive = 0;
        b->momentum = 0;
    }

    if (f->round >= f->opt.rounds) {
        int votes0 = 0, votes1 = 0;
        for (int j = 0; j < 3; j++) {
            if (f->judge_total[j][0] > f->judge_total[j][1]) votes0++;
            else if (f->judge_total[j][1] > f->judge_total[j][0]) votes1++;
        }
        if (votes0 == votes1) finish(f, RES_DRAW, -1);
        else finish(f, RES_DECISION, votes0 > votes1 ? 0 : 1);
        return;
    }
    f->between_rounds = 1;
    f->round++;
}

/* ------------------------------------------------------------------ corner */

static int score_diff(const Fight *f, int who) { return f->total[who] - f->total[!who]; }

int fight_corner_options(const Fight *f, int who, char L[8][48], char K[8])
{
    const FightBoxer *me = &f->b[who], *op = &f->b[!who];
    int n = 0;
    int inj = op->injury, mine = me->injury;
#define ADD(k, ...) do { K[n] = k; snprintf(L[n], 48, __VA_ARGS__); n++; } while (0)
    if (inj == INJ_NONE || inj == INJ_BRUISED_RIB || inj == INJ_BROKEN_RIB) ADD('Q', "Q - Punch to his head.");
    else ADD('Q', "Q - Work on that %s.", (inj == INJ_CUT_NOSE || inj == INJ_BLOODY_NOSE) ? "nose" : "eye");
    if (inj == INJ_BRUISED_RIB || inj == INJ_BROKEN_RIB) ADD('W', "W - Hit him in the ribs.");
    else ADD('W', "W - Work on his body.");
    if (op->live.head_target > 90) {
        int p = op->rec.best_punch;
        ADD('E', "E - Duck, watch out for his %s.",
            p == 2 ? "uppercut" : (p == 3 || p == 6) ? "right" : (p == 4 || p == 7) ? "left" : "jab");
    } else if (mine == INJ_BRUISED_RIB || mine == INJ_BROKEN_RIB) ADD('E', "E - Don't let him get to your ribs.");
    else if (mine != INJ_NONE && mine != INJ_GLAZED)
        ADD('E', "E - Cover up, protect that %s.", (mine == INJ_CUT_NOSE || mine == INJ_BLOODY_NOSE) ? "nose" : "eye");
    else ADD('E', "E - Fight defensively, rest up.");
    if (me->live.skill < 80) ADD('A', "A - Don't throw wild punches, aim!");
    else if (me->live.jab_bias < me->der.base.jab_bias + 20) ADD('A', "A - Jab, keep him away from you.");
    else ADD('A', "A - Back off, wait for an opening.");
    if (score_diff(f, who) < 0) ADD('S', "S - We're behind, nail him.");
    else if (op->live.energy < 35) ADD('S', "S - He's a gonner, finish him off.");
    else ADD('S', "S - Go for the knockout.");
    if (f->round > 2) {
        if (me->live.energy < 45) ADD('D', "D - Dump ice water down his pants.");
        else ADD('D', "D - You're doing good, Champ.");
        ADD('X', "X - Take a dive, we %s", score_diff(f, who) < 0 ? "lost anyway." : "need the money.");
    }
    if (mine == INJ_SWOLLEN_EYE || mine == INJ_EYE_SHUT) ADD('C', "C - Let me put ice on your eye.");
    else if (mine == INJ_BAD_GASH || mine == INJ_BLOODY_NOSE) ADD('C', "C - Let me stop the bleeding.");
#undef ADD
    return n;
}

void fight_apply_advice(Fight *f, int who, char key, int variant)
{
    FightBoxer *b = &f->b[who];
    Ratings *r = &b->live, *base = &b->der.base;
    switch (key) {                       /* OVR 5408 */
    case 'Q':
        if (r->head_target < 100) { r->head_target += 20; r->defence -= 5; r->jab_bias -= 15; }
        break;
    case 'W':
        if (r->head_target > 30) { r->head_target -= 20; r->defence -= 5; r->jab_bias = base->jab_bias; }
        break;
    case 'E':
        b->defensive = 1;
        r->aggression -= 70; r->openness -= 15; r->defence += 40; r->energy += 30;
        r->vulnerable -= 10; b->momentum = -40; r->jab_bias = base->jab_bias;
        if (b->injury != INJ_GLAZED) b->injury = INJ_NONE;
        break;
    case 'A':
        if (variant == 2) { r->skill += 10; r->jab_bias += 15; r->head_target += 10; }
        else {
            b->defensive = 1;
            r->aggression -= 40; r->openness -= 10; r->defence += 25; r->energy += 20; b->momentum = -20;
        }
        break;
    case 'S':
        b->all_out = 1;
        r->aggression = 115; r->openness = 40; r->defence = 10; r->vulnerable += 5;
        b->momentum = 60; r->energy -= 20;
        break;
    case 'D':
        r->aggression += 10; r->energy += 10; r->vulnerable -= 5; r->defence = base->defence;
        break;
    case 'X':
        r->vulnerable = 120; r->openness = 100; r->toughness = 0;
        break;
    case 'C':
        if (b->injury == INJ_SWOLLEN_EYE || b->injury == INJ_EYE_SHUT ||
            b->injury == INJ_BAD_GASH || b->injury == INJ_BLOODY_NOSE) {
            b->injury--; r->skill += 5; r->aggression += 10;
        }
        break;
    }
}

char fight_auto_advice(Fight *f, int who)
{
    FightBoxer *me = &f->b[who], *op = &f->b[!who];
    if (me->injury == INJ_EYE_SHUT || me->injury == INJ_BAD_GASH || me->injury == INJ_BLOODY_NOSE ||
        me->injury == INJ_SWOLLEN_EYE) return 'C';
    if (f->round == f->opt.rounds && score_diff(f, who) < 0) return 'S';
    if (op->live.energy < 35) return 'S';
    if (me->live.energy < 45) return f->round > 2 ? 'D' : 'E';
    if (op->injury == INJ_BRUISED_RIB || op->injury == INJ_BROKEN_RIB) return 'W';
    if (op->injury != INJ_NONE) return 'Q';
    return fight_random(f) < 64 ? 'Q' : 'W';
}

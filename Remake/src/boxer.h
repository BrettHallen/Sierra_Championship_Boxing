/* Boxer records and the ratings the original derives from them.
 * Formulas are documented in docs/GAMEPLAY.md section 4. */
#ifndef CB_BOXER_H
#define CB_BOXER_H

typedef struct {
    int used;
    char name[10];
    /* fields of the 512-byte BOXING.DAT record, in file order */
    int ko_defeats, reach, height, character, age, weight, fights, kos;
    int absorbs, best_punch, injured, killer, defence, accuracy, losses;
    int hitter, starter, aggressive, tires, hand_speed, auto_update;
} BoxerRecord;

/* Live ratings, named after what they do in the fight engine. */
typedef struct {
    int skill;        /* 8405 */
    int aggression;   /* 8409 */
    int openness;     /* 840D */
    int defence;      /* 8411 */
    int energy;       /* 8415 */
    int toughness;    /* 8419 */
    int power;        /* 841D */
    int jab_bias;     /* 8421 */
    int vulnerable;   /* 8425 */
    int head_target;  /* 8429 */
} Ratings;

typedef struct {
    Ratings base;     /* 1D70.. block, computed once per fight */
    int injury_threshold;  /* 1D98 */
    int killer_chance;     /* 1D9C */
    int fatigue;           /* 1DA4 */
} DerivedBoxer;

void boxer_parse(const unsigned char *sector, BoxerRecord *r);
void boxer_derive(const BoxerRecord *r, DerivedBoxer *d);
const char *punch_name(int best_punch);
const char *character_name(int c);

#endif

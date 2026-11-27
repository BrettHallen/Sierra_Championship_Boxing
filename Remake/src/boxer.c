#include "boxer.h"
#include <string.h>

static int w(const unsigned char *s, int i) { return (short)(s[i * 2] | (s[i * 2 + 1] << 8)); }

void boxer_parse(const unsigned char *s, BoxerRecord *r)
{
    char name[10];
    memcpy(name, r->name, sizeof name);
    memset(r, 0, sizeof *r);
    memcpy(r->name, name, sizeof name);
    r->ko_defeats = w(s, 0);  r->reach = w(s, 1);      r->height = w(s, 2);
    r->character = w(s, 3);   r->age = w(s, 4);        r->weight = w(s, 5);
    r->fights = w(s, 6);      r->kos = w(s, 7);        r->absorbs = w(s, 8);
    r->best_punch = w(s, 9);  r->injured = w(s, 10);   r->killer = w(s, 11);
    r->defence = w(s, 12);    r->accuracy = w(s, 13);  r->losses = w(s, 14);
    r->hitter = w(s, 15);     r->starter = w(s, 16);   r->aggressive = w(s, 17);
    r->tires = w(s, 18);      r->hand_speed = w(s, 19); r->auto_update = w(s, 20);
    /* An eliminated boxer is an all-zero record. */
    r->used = r->character >= 1 && r->character <= 4 && r->age > 0;
}

void boxer_derive(const BoxerRecord *r, DerivedBoxer *d)
{
    Ratings *b = &d->base;
    memset(d, 0, sizeof *d);
    /* FGT 03A0..06DA */
    d->injury_threshold = 127 - r->injured;
    switch (r->killer) {
    case 1: d->killer_chance = 40; break;
    case 2: d->killer_chance = 70; break;
    case 3: d->killer_chance = 90; break;
    default: d->killer_chance = 110; break;
    }
    b->head_target = r->best_punch == 8 ? 45 : r->best_punch == 1 ? 65 : 85;
    b->vulnerable = 3 * (5 - r->absorbs);
    if (r->age > 36) b->vulnerable *= 2;
    b->jab_bias = r->best_punch == 6 ? 100 : 80;
    b->toughness = r->fights / (r->ko_defeats + 1) + 4 * r->absorbs;
    b->defence = 10 * r->defence;
    b->openness = 65 - b->defence;
    b->aggression = 20 * r->aggressive;
    b->skill = 15 * r->accuracy + 10 * r->hand_speed + r->fights - r->losses;
    b->power = 10 * r->hitter;
    /* FGT 13BC..1500 */
    d->fatigue = r->age / 2 + r->weight / 5 - 38 + 4 * (3 - r->tires);
    b->energy = 110 - d->fatigue - r->weight / 10 + 20 * r->tires;
    b->power += 60 * r->kos / (r->fights + 1);
}

const char *punch_name(int p)
{
    static const char *n[] = {"?", "Jab", "Uppercut", "Overhand right", "Left hook",
                              "Body punch", "Right hand punch", "Left hand punch", "None"};
    return (p >= 1 && p <= 8) ? n[p] : n[0];
}

const char *character_name(int c)
{
    static const char *n[] = {"?", "Black", "White", "Cartoon", "Kangaroo"};
    return (c >= 1 && c <= 4) ? n[c] : n[0];
}

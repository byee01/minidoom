// minidoom: touch-friendly assists replacing the FIRE and USE buttons.
//  - auto-fire: hold fire only while an enemy is in the crosshair (Doom's own P_AimLineAttack,
//    which already handles vertical aim), with a small horizontal aim assist.
//  - auto-use: open closed doors / press switches the player walks up to and faces.
// GPLv2, part of the doomgeneric fork.

#include <stdlib.h>

#include "doomkeys.h"
#include "doomstat.h"
#include "d_player.h"
#include "info.h"
#include "m_fixed.h"
#include "p_local.h"
#include "r_state.h"
#include "tables.h"
#include "lens_state.h"

extern mobj_t *linetarget;
void lens_post_key(int pressed, int key);
void lens_tap_key(int key);

static int assist_on = 1;
static int fire_held = 0;
static line_t *last_used_line = NULL;
static int last_used_tic = -1000;
static line_t *found_line;
static int found_side;

#define ASSIST_MAX_TURN (ANG1 * 3 / 2)    // aim assist turn per tic
#define ASSIST_CONE_DEG 4                 // assist searches this far either side

static int is_enemy(mobj_t *t)
{
    return t && t->health > 0 && (t->flags & MF_SHOOTABLE) &&
           ((t->flags & MF_COUNTKILL) || t->type == MT_SKULL);
}

static mobj_t *enemy_at(mobj_t *mo, angle_t ang, fixed_t range)
{
    P_AimLineAttack(mo, ang, range);
    return is_enemy(linetarget) ? linetarget : NULL;
}

static void set_fire(int on)
{
    if (on == fire_held)
        return;
    fire_held = on;
    lens_post_key(on, KEY_FIRE);
}

// ---------------------------------------------------------------- auto-use
static boolean PTR_FindUse(intercept_t *in)
{
    if (!in->d.line->special)
    {
        P_LineOpening(in->d.line);
        return openrange > 0;      // keep looking through openings, stop at walls
    }
    found_line = in->d.line;
    found_side = P_PointOnLineSide(players[consoleplayer].mo->x, players[consoleplayer].mo->y, in->d.line);
    return false;
}

static int is_manual_door(int special)
{
    switch (special)
    {
        case 1: case 26: case 27: case 28: case 31: case 32: case 33: case 34: case 117: case 118:
            return 1;
        default:
            return 0;
    }
}

static int auto_use(player_t *p)
{
    mobj_t *mo = p->mo;
    int a = mo->angle >> ANGLETOFINESHIFT;
    found_line = NULL;
    P_PathTraverse(mo->x, mo->y, mo->x + (USERANGE >> FRACBITS) * finecosine[a],
                   mo->y + (USERANGE >> FRACBITS) * finesine[a], PT_ADDLINES, PTR_FindUse);
    if (!found_line || found_side != 0)
        return 0;
    if (is_manual_door(found_line->special))
    {
        sector_t *door = found_line->backsector;
        // Only open a closed, idle door: using an open one would close it on the player.
        if (!door || door->specialdata || door->ceilingheight > door->floorheight)
            return 0;
    }
    else if (found_line == last_used_line && gametic - last_used_tic < 2 * TICRATE)
    {
        return 0;                  // switches / locked doors: don't spam
    }
    last_used_line = found_line;
    last_used_tic = gametic;
    lens_tap_key(KEY_USE);
    return 1;
}

// ---------------------------------------------------------------- per-tic update
// Runs after each game tic; the resulting key state applies to the next tic.
void lens_assist_update(int32_t *s)
{
    player_t *p = &players[consoleplayer];
    s[LS_TARGET] = 0;
    s[LS_AUTOFIRE] = 0;
    if (!assist_on || !p->mo || p->playerstate != PST_LIVE || gamestate != GS_LEVEL)
    {
        set_fire(0);
        return;
    }
    mobj_t *mo = p->mo;
    int melee = p->readyweapon == wp_fist || p->readyweapon == wp_chainsaw;
    fixed_t range = melee ? MELEERANGE + FRACUNIT * 8 : 16 * 64 * FRACUNIT;

    mobj_t *t = enemy_at(mo, mo->angle, range);
    if (!t)
    {
        // aim assist: nudge toward an enemy within the cone. 4 traversals, every other tic
        // (each P_AimLineAttack is a full path traversal; 8 per tic cost ~4 ms/frame in Lens).
        for (int d = 2; d <= ASSIST_CONE_DEG && !t && (gametic & 1) == 0; d += 2)
        {
            angle_t off = ANG1 * d;
            mobj_t *l = enemy_at(mo, mo->angle + off, range);
            mobj_t *r = enemy_at(mo, mo->angle - off, range);
            t = l ? l : r;
        }
        if (t)
        {
            angle_t want = R_PointToAngle2(mo->x, mo->y, t->x, t->y);
            int delta = (int)(want - mo->angle);
            if (delta > (int)ASSIST_MAX_TURN) delta = ASSIST_MAX_TURN;
            if (delta < -(int)ASSIST_MAX_TURN) delta = -(int)ASSIST_MAX_TURN;
            mo->angle += delta;
            t = enemy_at(mo, mo->angle, range);
        }
    }
    int fire = t != NULL;
    // Never fire a rocket into something point blank.
    if (fire && p->readyweapon == wp_missile && P_AproxDistance(t->x - mo->x, t->y - mo->y) < 160 * FRACUNIT)
        fire = 0;
    s[LS_TARGET] = t != NULL;
    s[LS_AUTOFIRE] = fire;
    set_fire(fire);
    if (auto_use(p))
        s[LS_AUTOUSE_COUNT]++;
}

void lens_assist_reset(void)
{
    fire_held = 0;
    last_used_line = NULL;
    last_used_tic = -1000;
}

__attribute__((used, visibility("default"))) void lens_set_assist(int on)
{
    assist_on = on;
    if (!on && fire_held)
        set_fire(0);
}

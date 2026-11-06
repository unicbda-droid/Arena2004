#include "hud.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static Color white = {0.94f, 0.96f, 1.0f, 1.0f};
static Color dim = {0.62f, 0.68f, 0.76f, 1.0f};
static Color accent = {1.0f, 0.62f, 0.16f, 1.0f};
static Color cyan = {0.22f, 0.78f, 1.0f, 1.0f};
static Color red = {0.95f, 0.25f, 0.20f, 1.0f};
static Color green = {0.35f, 0.92f, 0.45f, 1.0f};
static bool show_fps = true;
static bool show_crosshair = true;
static bool show_netgraph = false;
void hud_set_fps_visible(bool visible) { show_fps = visible; }
void hud_set_crosshair_visible(bool visible) { show_crosshair = visible; }
void hud_set_netgraph_visible(bool visible) { show_netgraph = visible; }

static void bar(Ui* ui, float x, float y, float w, float h, float t, Color c, Color bg) {
    ui_rect(ui, x, y, w, h, bg);
    ui_rect(ui, x, y, w * clampf(t, 0, 1), h, c);
    ui_rect_outline(ui, x, y, w, h, col(0, 0, 0, 0.55f), 1);
    /* shine */
    ui_rect(ui, x, y, w * clampf(t, 0, 1), h * 0.34f, col(c.r * 1.4f, c.g * 1.4f, c.b * 1.4f, 0.35f));
}

static void weapon_icon(Ui* ui, float x, float y, float s, int wi, Color c, bool owned) {
    Color cc = owned ? c : col(0.35f, 0.38f, 0.42f, 0.55f);
    switch (wi) {
    case WPN_AR:
        ui_rect(ui, x + s * 0.10f, y + s * 0.36f, s * 0.75f, s * 0.16f, cc);
        ui_rect(ui, x + s * 0.55f, y + s * 0.28f, s * 0.28f, s * 0.10f, cc);
        ui_rect(ui, x + s * 0.22f, y + s * 0.48f, s * 0.12f, s * 0.22f, cc);
        break;
    case WPN_SHOCK:
        ui_rect(ui, x + s * 0.12f, y + s * 0.34f, s * 0.72f, s * 0.20f, cc);
        for (int i = 0; i < 3; i++) ui_rect(ui, x + s * (0.20f + i * 0.20f), y + s * 0.28f, s * 0.08f, s * 0.32f, col(cc.r, cc.g, cc.b, 0.8f));
        ui_rect(ui, x + s * 0.06f, y + s * 0.38f, s * 0.10f, s * 0.12f, cc);
        break;
    case WPN_ROCKET:
        ui_rect(ui, x + s * 0.10f, y + s * 0.34f, s * 0.70f, s * 0.22f, cc);
        ui_rect(ui, x + s * 0.80f, y + s * 0.29f, s * 0.12f, s * 0.32f, cc);
        ui_rect(ui, x + s * 0.22f, y + s * 0.52f, s * 0.10f, s * 0.20f, cc);
        break;
    case WPN_FLAK:
        ui_rect(ui, x + s * 0.10f, y + s * 0.32f, s * 0.48f, s * 0.24f, cc);
        ui_rect(ui, x + s * 0.58f, y + s * 0.26f, s * 0.28f, s * 0.36f, cc);
        ui_rect(ui, x + s * 0.20f, y + s * 0.54f, s * 0.12f, s * 0.18f, cc);
        break;
    case WPN_LIGHTNING:
        ui_rect(ui, x + s * 0.12f, y + s * 0.40f, s * 0.66f, s * 0.14f, cc);
        ui_rect(ui, x + s * 0.66f, y + s * 0.34f, s * 0.24f, s * 0.06f, cc);
        ui_rect(ui, x + s * 0.66f, y + s * 0.54f, s * 0.24f, s * 0.06f, cc);
        for (int i = 0; i < 2; i++) ui_ring(ui, x + s * (0.28f + i * 0.24f), y + s * 0.30f, s * 0.26f, s * 0.36f, col(cc.r, cc.g, cc.b, 0.7f));
        break;
    }
}

static const char* weapon_ammo_name(int wi) {
    switch (wi) {
    case WPN_AR: return "ASSAULT RIFLE";
    case WPN_SHOCK: return "SHOCK RIFLE";
    case WPN_ROCKET: return "ROCKET LAUNCHER";
    case WPN_FLAK: return "FLAK CANNON";
    case WPN_LIGHTNING: return "LIGHTNING GUN";
    }
    return "";
}

void hud_draw_weapon_bar(Ui* ui, Game* g, int w, int h) {
    Player* p = &g->players[g->local_player];
    float baseX = (float)w - 36;
    float y = (float)h - 120;
    for (int i = 0; i < WPN_COUNT; i++) {
        float yy = y - i * 42;
        bool sel = p->weapon == i;
        bool owned = p->has[i];
        Color bg = sel ? col(0.16f, 0.30f, 0.44f, 0.92f) : col(0.04f, 0.06f, 0.08f, 0.72f);
        ui_rect(ui, baseX - 150, yy, 150, 36, bg);
        ui_rect(ui, baseX - 150, yy, 3, 36, sel ? accent : col(0.24f, 0.28f, 0.33f, 1));
        weapon_icon(ui, baseX - 142, yy + 4, 28, i, weapon_defs[i].color, owned);
        char num[20]; snprintf(num, sizeof(num), "%d/%d", p->mag[i], p->ammo[i]);
        ui_text_right(ui, baseX - 10, yy + 12, num, 10, owned ? (sel ? white : dim) : col(0.35f, 0.38f, 0.42f, 0.7f));
        ui_text(ui, baseX - 106, yy + 10, weapon_defs[i].short_name, 11, owned ? (sel ? white : dim) : col(0.35f, 0.38f, 0.42f, 0.5f));
    }
}

void hud_draw(Ui* ui, Game* g, int w, int h) {
    Player* me = &g->players[g->local_player];
    float cx = w * 0.5f;
    float cy = h * 0.5f;

    /* ---------- damage flash ---------- */
    if (g->damage_flash > 0) {
        float a = clampf(g->damage_flash, 0, 1) * 0.34f;
        ui_rect_grad(ui, 0, 0, (float)w, h * 0.35f, col(0.7f, 0.03f, 0.02f, a), col(0.7f, 0.03f, 0.02f, 0));
        ui_rect_grad(ui, 0, h * 0.65f, (float)w, h * 0.35f, col(0.7f, 0.03f, 0.02f, 0), col(0.7f, 0.03f, 0.02f, a));
        ui_rect_grad(ui, 0, 0, w * 0.3f, (float)h, col(0.7f, 0.03f, 0.02f, a * 0.7f), col(0.7f, 0.03f, 0.02f, 0));
        ui_rect_grad(ui, w * 0.7f, 0, w * 0.3f, (float)h, col(0.7f, 0.03f, 0.02f, 0), col(0.7f, 0.03f, 0.02f, a * 0.7f));
    }
    if (!me->alive) {
        ui_rect(ui, 0, 0, (float)w, (float)h, col(0.20f, 0.02f, 0.02f, 0.34f));
    }

    /* ---------- crosshair ---------- */
    if (me->alive && show_crosshair) {
        bool hit = g->hitmarker > 0;
        Color ch = hit ? col(1.0f, 0.35f, 0.25f, 1.0f) : col(0.95f, 0.98f, 1.0f, 0.92f);
        float gap = 6 + me->recoil * 5.0f + (v3len(v3(me->vel.x, 0, me->vel.z)) > 2 ? 3.0f : 0.0f);
        float len = 7;
        float th = 2;
        ui_rect(ui, cx - gap - len, cy - th * 0.5f, len, th, ch);
        ui_rect(ui, cx + gap, cy - th * 0.5f, len, th, ch);
        ui_rect(ui, cx - th * 0.5f, cy - gap - len, th, len, ch);
        ui_rect(ui, cx - th * 0.5f, cy + gap, th, len, ch);
        ui_rect(ui, cx - 1, cy - 1, 2, 2, ch);
        if (hit) {
            float t = g->hitmarker / 0.28f;
            float r = 12 + (1 - t) * 12;
            Color hc = col(1.0f, 0.4f, 0.2f, t);
            float d = r * 0.7071f;
            ui_line(ui, v2(cx - d, cy - d), v2(cx - d + 8, cy - d + 8), hc, 2);
            ui_line(ui, v2(cx + d, cy - d), v2(cx + d - 8, cy - d + 8), hc, 2);
            ui_line(ui, v2(cx - d, cy + d), v2(cx - d + 8, cy + d - 8), hc, 2);
            ui_line(ui, v2(cx + d, cy + d), v2(cx + d - 8, cy + d - 8), hc, 2);
        }
    }

    /* ---------- top score strip ---------- */
    float stripW = 360;
    float sx = cx - stripW * 0.5f;
    ui_rect(ui, sx, 0, stripW, 56, col(0.02f, 0.03f, 0.045f, 0.78f));
    ui_rect(ui, sx, 53, stripW, 3, col(accent.r, accent.g, accent.b, 0.85f));
    char tbuf[32]; game_time_string(g->time_limit - g->match_time, tbuf, sizeof(tbuf));
    if (g->game_mode == GM_INVASION) {
        int survivors = 0;
        for (int i = 0; i < g->num_players; i++)
            if (g->players[i].active && g->players[i].alive && !g->players[i].zombie) survivors++;
        if (g->invasion_wave < 1) snprintf(tbuf, sizeof(tbuf), "GET READY");
        else snprintf(tbuf, sizeof(tbuf), "WAVE %d / %d", g->invasion_wave, g->invasion_wave_max);
        (void)survivors;
    }
    ui_text_center(ui, cx, 6, tbuf, 25, (g->game_mode != GM_INVASION && g->match_time > g->time_limit - 60) ? red : white);
    char sub[128];
    if (g->game_mode == GM_INVASION) {
        int survivors = 0;
        for (int i = 0; i < g->num_players; i++)
            if (g->players[i].active && g->players[i].alive && !g->players[i].zombie) survivors++;
        snprintf(sub, sizeof(sub), "HOSTILES %d   QUEUED %d   SURVIVORS %d   KILLS %d",
                 g->invasion_alive, g->invasion_spawn_left, survivors, g->invasion_killed);
    } else if (game_mode_team_based(g->game_mode)) {
        snprintf(sub, sizeof(sub), "%s   RED %d : %d BLUE   LIMIT %d",
                 game_mode_short(g->game_mode), g->team_score[0], g->team_score[1], (int)g->score_limit);
    } else if (g->game_mode == GM_BATTLE_ROYALE) {
        snprintf(sub, sizeof(sub), "BR   ALIVE %d / %d   ZONE %.0fs", g->br_alive, g->br_total, fmaxf(g->zone_timer, 0));
    } else {
        snprintf(sub, sizeof(sub), "%s   FRAG LIMIT %d", game_mode_short(g->game_mode), g->frag_limit);
    }
    ui_text_center(ui, cx, 34, sub, 10, dim);
    ui_text_center(ui, cx, 62, game_mode_name(g->game_mode), 9, col(0.98f, 0.62f, 0.16f, 0.9f));

    /* local score left of strip */
    char sc[32];
    if (game_mode_team_based(g->game_mode) && me->team >= 0)
        snprintf(sc, sizeof(sc), "%d", g->team_score[me->team]);
    else
        snprintf(sc, sizeof(sc), "%d", me->frags);
    ui_text_right(ui, sx - 18, 6, sc, 30, game_mode_team_based(g->game_mode) ? (me->team == 0 ? red : cyan) : white);
    ui_text_right(ui, sx - 18, 38, game_mode_team_based(g->game_mode) ? "TEAM" : (g->game_mode == GM_INVASION ? "KILLS" : "FRAGS"), 9, dim);
    char dc[32];
    if (game_mode_team_based(g->game_mode) && me->team >= 0)
        snprintf(dc, sizeof(dc), "%d", g->team_score[1 - me->team]);
    else
        snprintf(dc, sizeof(dc), "%d", me->deaths);
    ui_text(ui, sx + stripW + 18, 6, dc, 30, game_mode_team_based(g->game_mode) ? (me->team == 0 ? cyan : red) : white);
    ui_text(ui, sx + stripW + 18, 38, game_mode_team_based(g->game_mode) ? "ENEMY" : "DEATHS", 9, dim);

    /* ---------- kill feed ---------- */
    float ky = 70;
    for (int i = 0; i < MAX_KILLFEED; i++) {
        KillFeedItem* k = &g->killfeed[i];
        if (k->time <= 0) continue;
        float a = clampf(k->time, 0, 1);
        float tw = ui_text_width(ui, k->text, 12) + 18;
        ui_rect(ui, 22, ky, tw, 23, col(0.02f, 0.03f, 0.05f, 0.66f * a));
        ui_rect(ui, 22, ky, 3, 23, col(accent.r, accent.g, accent.b, a));
        ui_text(ui, 32, ky + 4, k->text, 12, col(0.88f, 0.92f, 0.98f, a));
        ky += 26;
    }

    /* ---------- vehicle panel ---------- */
    if (me->alive && me->in_vehicle && me->vehicle >= 0 && me->vehicle < g->vehicle_count) {
        Vehicle* v = &g->vehicles[me->vehicle];
        const VehicleDef* vd = &vehicle_defs[v->type];
        float vx = 34, vy = (float)h - 232;
        ui_rect(ui, vx - 12, vy - 10, 320, 92, col(0.02f, 0.035f, 0.05f, 0.80f));
        ui_rect(ui, vx - 12, vy - 10, 4, 92, col(accent.r, accent.g, accent.b, 0.95f));
        ui_text(ui, vx, vy - 4, game_vehicle_name(v->type), 20, white);
        char vs[32]; snprintf(vs, sizeof(vs), "%s", game_vehicle_short(v->type));
        ui_text_right(ui, vx + 296, vy - 2, vs, 10, dim);
        float hpfrac = v->max_hp > 0 ? clampf(v->hp / v->max_hp, 0, 1) : 0.0f;
        Color hpc = hpfrac > 0.5f ? green : (hpfrac > 0.22f ? col(1.0f, 0.78f, 0.2f, 1) : red);
        char vh[32]; snprintf(vh, sizeof(vh), "%d / %d", (int)ceilf(v->hp), (int)ceilf(v->max_hp));
        ui_text(ui, vx, vy + 21, "HULL", 9, dim);
        bar(ui, vx + 40, vy + 23, 200, 10, hpfrac, hpc, col(0.06f, 0.09f, 0.11f, 0.9f));
        ui_text(ui, vx + 248, vy + 21, vh, 10, hpc);
        ui_text(ui, vx, vy + 42, vd->splash > 0.1f ? "E = AUSSTEIGEN   FEUER = SALVE" : "E = AUSSTEIGEN   FEUER = PLASMA",
                9, col(0.70f, 0.80f, 0.90f, 0.95f));
        /* module pips: wheels, engine, weapon. A dead module turns red and
         * empty, so the driver instantly sees why the buggy pulls or is silent. */
        static const char* MOD_LBL[VEH_MODULES] = { "FL", "FR", "BL", "BR", "ENG", "WPN" };
        for (int k = 0; k < VEH_MODULES; k++) {
            float mx = vx + (float)k * 49.0f;
            float f = v->modules[k].max_hp > 0.0f ? clampf(v->modules[k].hp / v->modules[k].max_hp, 0, 1) : 0.0f;
            Color mc = v->modules[k].destroyed ? col(0.95f, 0.22f, 0.16f, 1) :
                       (f > 0.55f ? col(0.55f, 0.85f, 0.55f, 1) : col(1.0f, 0.72f, 0.18f, 1));
            ui_text(ui, mx, vy + 60, MOD_LBL[k], 8, v->modules[k].destroyed ? mc : dim);
            bar(ui, mx, vy + 72, 42, 5, f, mc, col(0.07f, 0.08f, 0.09f, 0.9f));
        }
    }

    /* ---------- bottom left: health / armor ---------- */
    float bx = 34, by = (float)h - 118;
    ui_rect(ui, bx - 12, by - 10, 320, 100, col(0.02f, 0.03f, 0.045f, 0.72f));
    ui_rect(ui, bx - 12, by - 10, 4, 100, col(green.r, green.g, green.b, 0.9f));

    char hbuf[16]; snprintf(hbuf, sizeof(hbuf), "%d", (int)ceilf(me->health));
    ui_text(ui, bx, by - 4, hbuf, 42, me->health < 30 ? red : white);
    ui_text(ui, bx + ui_text_width(ui, hbuf, 42) + 8, by + 20, "HEALTH", 10, dim);
    float maxhp = game_player_max_health(me);
    float maxarmor = game_player_max_armor(me);
    bar(ui, bx + 100, by + 8, 190, 14, me->health / maxhp, me->health < 30 ? red : green, col(0.06f, 0.10f, 0.12f, 0.9f));

    char abuf[16]; snprintf(abuf, sizeof(abuf), "%d", (int)ceilf(me->armor));
    ui_text(ui, bx, by + 46, abuf, 26, me->armor > 0 ? cyan : dim);
    ui_text(ui, bx + ui_text_width(ui, abuf, 26) + 8, by + 58, "SHIELD", 10, dim);
    bar(ui, bx + 100, by + 56, 190, 12, me->armor / fmaxf(maxarmor, 1.0f), cyan, col(0.06f, 0.08f, 0.14f, 0.9f));

    if (me->protection > 0) {
        char pr[32]; snprintf(pr, sizeof(pr), "SPAWN PROTECTION %.1f", me->protection);
        ui_text(ui, bx + 100, by + 74, pr, 9, col(0.6f, 0.9f, 1.0f, 0.9f));
    }

    /* ---------- bottom right: weapon + ammo ---------- */
    int wi = me->weapon;
    ui_rect(ui, w - 322, h - 118, 290, 100, col(0.02f, 0.03f, 0.045f, 0.72f));
    ui_rect(ui, w - 36, h - 118, 4, 100, col(accent.r, accent.g, accent.b, 0.9f));
    weapon_icon(ui, w - 314, h - 110, 44, wi, weapon_defs[wi].color, true);
    ui_text(ui, w - 258, h - 110, weapon_ammo_name(wi), 12, white);
    char ammo[32]; snprintf(ammo, sizeof(ammo), "%d", me->mag[wi]);
    ui_text_right(ui, w - 44, h - 114, ammo, 46, me->mag[wi] <= 0 ? red : white);
    char mx[32]; snprintf(mx, sizeof(mx), "/ %d", me->ammo[wi]);
    ui_text_right(ui, w - 44, h - 66, mx, 11, dim);
    ui_text(ui, w - 258, h - 92, me->reload_timer > 0.0f ? "NACHLADEN..." : "MAGAZIN / RESERVE", 9,
            me->reload_timer > 0.0f ? col(0.98f, 0.62f, 0.16f, 1) : dim);
    if (me->reload_timer > 0.0f && me->reload_weapon == wi) {
        float total = me->reload_total > 0.01f ? me->reload_total : 1.0f;
        float rl = 1.0f - clampf(me->reload_timer / total, 0.0f, 1.0f);
        ui_rect(ui, w - 318, h - 22, 286, 3, col(0.12f, 0.15f, 0.2f, 0.9f));
        ui_rect(ui, w - 318, h - 22, 286 * rl, 3, col(0.98f, 0.62f, 0.16f, 1));
    }

    /* ---------- RPG level / XP ---------- */
    if (g->game_mode == GM_RPG || g->game_mode == GM_INVASION) {
        char lv[64];
        snprintf(lv, sizeof(lv), "LEVEL %d", me->level > 0 ? me->level : 1);
        ui_text(ui, bx + 100, by + 90, lv, 11, col(0.45f, 1.0f, 0.60f, 1));
        float xpfrac = me->xp_next > 0 ? (float)me->xp / (float)me->xp_next : 0.0f;
        bar(ui, bx + 165, by + 92, 125, 7, xpfrac, col(0.45f, 0.95f, 0.55f, 1), col(0.06f, 0.10f, 0.09f, 0.9f));
        char xb[64]; snprintf(xb, sizeof(xb), "%d / %d XP   //   %d CR   //   %s", me->xp, me->xp_next,
                              me->credits, game_rank_name(me->rank));
        ui_text_right(ui, bx + 290, by + 100, xb, 8, dim);
        if (me->vamp_flash > 0.05f) {
            ui_rect_outline(ui, bx + 100, by + 6, 190, 18, col(0.9f, 0.15f, 0.35f, 0.25f * me->vamp_flash), 1);
        }
    }

    /* ---------- objectives ---------- */
    if (g->node_count > 0) {
        float ox = cx - (float)g->node_count * 62.0f * 0.5f;
        for (int i = 0; i < g->node_count; i++) {
            ControlNode* nd = &g->nodes[i];
            if (!nd->active) continue;
            Color nc = nd->team == 0 ? red : (nd->team == 1 ? cyan : col(0.75f, 0.78f, 0.82f, 1));
            ui_rect(ui, ox + i * 62, (float)h - 172, 54, 26, col(0.02f, 0.03f, 0.05f, 0.72f));
            ui_rect(ui, ox + i * 62, (float)h - 172, 54, 3, nc);
            ui_text(ui, ox + i * 62 + 6, (float)h - 166, nd->name, 8, nc);
            if (nd->progress > 0.01f)
                bar(ui, ox + i * 62 + 4, (float)h - 152, 46, 4, nd->progress,
                    nd->attacker >= 0 ? (nd->attacker == 0 ? red : cyan) : accent, col(0.08f, 0.09f, 0.11f, 0.9f));
        }
    } else if (g->flags[0].active || g->flags[1].active) {
        for (int t = 0; t < 2; t++) {
            Flag* f = &g->flags[t];
            const char* st = f->carried ? "TAKEN" : (f->dropped ? "DROPPED" : "HOME");
            char fb[48]; snprintf(fb, sizeof(fb), "%s FLAG %s", t == 0 ? "RED" : "BLUE", st);
            ui_text(ui, cx - 120 + t * 130, (float)h - 172, fb, 9, t == 0 ? red : cyan);
        }
    }

    /* ---------- Fortnite building HUD ---------- */
    if (me->alive && me->build_mode) {
        float bw = 430, bh = 92;
        float bxp = cx - bw * 0.5f, byp = (float)h - 320;
        ui_rect(ui, bxp, byp, bw, bh, col(0.02f, 0.035f, 0.05f, 0.86f));
        ui_rect(ui, bxp, byp, bw, 3, col(0.30f, 1.0f, 0.45f, 0.9f));
        ui_text(ui, bxp + 12, byp + 9, "BAUMODUS", 12, col(0.42f, 1.0f, 0.55f, 1));
        ui_text_right(ui, bxp + bw - 12, byp + 9, "B = AUS", 9, dim);
        static const char* PIECES[3] = { "1 WAND", "2 BODEN", "3 RAMPE" };
        for (int i = 0; i < 3; i++) {
            bool sel = me->build_sel == i;
            ui_rect(ui, bxp + 10 + i * 98, byp + 28, 92, 22,
                    sel ? col(0.16f, 0.42f, 0.24f, 0.95f) : col(0.05f, 0.08f, 0.10f, 0.9f));
            ui_text(ui, bxp + 18 + i * 98, byp + 33, PIECES[i], 10, sel ? white : dim);
        }
        ui_text(ui, bxp + 308, byp + 33, game_build_piece_name(me->build_sel), 10, col(0.85f, 0.92f, 1.0f, 1));
        ui_text(ui, bxp + 12, byp + 56, "MATERIAL  MAUSRAD", 8, dim);
        static const int MAT_KEYS[3] = { 0, 1, 2 };
        for (int i = 0; i < 3; i++) {
            int mat = MAT_KEYS[i];
            bool sel = me->build_mat == mat;
            float f = fminf(1.0f, (float)me->mats[mat] / 200.0f);
            Color mc = mat == 0 ? col(0.72f, 0.48f, 0.24f, 1) :
                       (mat == 1 ? col(0.68f, 0.68f, 0.64f, 1) : col(0.60f, 0.72f, 0.86f, 1));
            ui_rect(ui, bxp + 92 + i * 88, byp + 54, 76, 14, col(0.06f, 0.08f, 0.10f, 0.9f));
            ui_rect(ui, bxp + 92 + i * 88, byp + 54, 76 * f, 14, mc);
            char mb[20]; snprintf(mb, sizeof(mb), "%d", me->mats[mat]);
            ui_text(ui, bxp + 96 + i * 88, byp + 72, game_build_material_name(mat), 8, sel ? mc : dim);
            ui_text_right(ui, bxp + 164 + i * 88, byp + 72, mb, 8, sel ? white : dim);
        }
        ui_text_right(ui, bxp + bw - 12, byp + 58, "R = DREHEN   FEUER = BAUEN", 8, col(0.72f, 0.82f, 0.95f, 0.95f));
    }

    /* ---------- Altis Life HUD ---------- */
    if (g->game_mode == GM_ALTIS && me->alive) {
        float lw = 322, lh = 104;
        float lx = 34, ly = (float)h - 232;
        ui_rect(ui, lx, ly, lw, lh, col(0.02f, 0.035f, 0.05f, 0.82f));
        ui_rect(ui, lx, ly, 4, lh, col(0.98f, 0.72f, 0.18f, 0.95f));
        ui_text(ui, lx + 14, ly + 8, "ALTIS LIFE", 14, col(0.98f, 0.86f, 0.42f, 1));
        char wb[64];
        snprintf(wb, sizeof(wb), "GESUCHT");
        ui_text(ui, lx + 148, ly + 11, wb, 8, dim);
        for (int i = 0; i < 5; i++)
            ui_text(ui, lx + 208 + i * 15, ly + 8, i < me->wanted ? "*" : "-", 12,
                    i < me->wanted ? col(1.0f, 0.30f, 0.22f, 1) : col(0.35f, 0.40f, 0.46f, 1));
        char cb[48], bb[48], ob[48];
        snprintf(cb, sizeof(cb), "%d $", me->cash);
        snprintf(bb, sizeof(bb), "%d $", me->bank);
        snprintf(ob, sizeof(ob), "%d", me->ore);
        ui_text(ui, lx + 14, ly + 34, "BAR", 8, dim);
        ui_text(ui, lx + 44, ly + 30, cb, 15, col(0.55f, 1.0f, 0.60f, 1));
        ui_text(ui, lx + 128, ly + 34, "BANK", 8, dim);
        ui_text(ui, lx + 162, ly + 30, bb, 15, col(0.45f, 0.82f, 1.0f, 1));
        ui_text(ui, lx + 240, ly + 34, "ERZ", 8, dim);
        ui_text(ui, lx + 266, ly + 30, ob, 15, col(0.78f, 0.72f, 0.62f, 1));
        const char* job = me->life_job == 1 ? (me->life_stage == 0 ? "LIEFERAUFTRAG: ZUM DEPOT" : "AUFTRAG AKTIV") : "ARBEITSLOS";
        ui_text(ui, lx + 14, ly + 56, job, 10, me->life_job ? col(0.45f, 1.0f, 0.60f, 1) : dim);
        if (g->life_near >= 0 && g->life_near < g->life_point_count) {
            LifePoint* lp = &g->life_points[g->life_near];
            char pr[96];
            snprintf(pr, sizeof(pr), "E = %s BENUTZEN", lp->name);
            ui_rect(ui, lx + 8, ly + 76, lw - 16, 22, col(0.10f, 0.16f, 0.10f, 0.9f));
            ui_text(ui, lx + 14, ly + 81, pr, 10, col(0.55f, 1.0f, 0.60f, 1));
        } else {
            ui_text(ui, lx + 14, ly + 80, "E = BENUTZEN   //   SAMMELN AN BAUMEN / STEINEN / SCHROTT", 7, col(0.52f, 0.58f, 0.66f, 0.9f));
        }
    }

    /* ---------- battle royale zone warning ---------- */
    if (g->game_mode == GM_BATTLE_ROYALE && me->alive) {
        float dx = me->pos.x - g->zone_center_x, dz = me->pos.z - g->zone_center_z;
        bool outside = (dx * dx + dz * dz) > g->zone_radius * g->zone_radius;
        char zb[128];
        if (outside) snprintf(zb, sizeof(zb), "OUTSIDE THE ZONE - MOVE TO THE CENTER");
        else snprintf(zb, sizeof(zb), "ZONE %.0fm   NEXT SHRINK %.0fs", g->zone_radius, fmaxf(g->zone_timer, 0));
        ui_text_center(ui, cx, (float)h - 208, zb, 11, outside ? col(1.0f, 0.35f, 0.25f, 1) : dim);
    }

    /* ---------- server zones / channels ---------- */
    if (g->zone_count > 1 || g->chan_count > 1) {
        char zc[160];
        snprintf(zc, sizeof(zc), "ZONE %s %d/%d   CH %s %d/%d   MEGA %d",
                 g->zone_label[g->zone_id], g->zone_pop[g->zone_id], g->zone_total,
                 g->chan_label[g->chan_id], g->chan_pop[g->chan_id], g->chan_count, g->zone_total);
        ui_text_right(ui, (float)w - 22, 90, zc, 9, col(0.45f, 0.56f, 0.68f, 0.85f));
    }

    /* ---------- netgraph ---------- */
    if (show_netgraph && (g->networked || g->ping > 0)) {
        float gw = 148, gh = 54, gx = (float)w - gw - 22, gy = (float)h - 168;
        ui_rect(ui, gx, gy, gw, gh, col(0.02f, 0.03f, 0.04f, 0.80f));
        ui_rect(ui, gx, gy, gw, 2, col(accent.r, accent.g, accent.b, 0.8f));
        char pg[64]; snprintf(pg, sizeof(pg), "PING %.0f ms  SNAP %d", g->ping, g->net_snapshots);
        ui_text(ui, gx + 7, gy + 6, pg, 8, white);
        for (int i = 0; i < 12; i++) {
            float hh = 4 + (float)((i * 37 + (int)g->ping) % 18);
            Color c = g->ping < 60 ? green : (g->ping < 140 ? accent : red);
            ui_rect(ui, gx + 8 + i * 11, gy + gh - 6 - hh, 8, hh, c);
        }
    }

    /* ---------- pickup message ---------- */
    if (g->pickup_msg_t > 0) {
        float a = clampf(g->pickup_msg_t, 0, 1);
        float tw = ui_text_width(ui, g->pickup_msg, 14) + 30;
        ui_rect(ui, cx - tw * 0.5f, h - 200, tw, 30, col(0.02f, 0.04f, 0.06f, 0.8f * a));
        ui_rect_outline(ui, cx - tw * 0.5f, h - 200, tw, 30, col(accent.r, accent.g, accent.b, 0.5f * a), 1);
        ui_text_center(ui, cx, h - 194, g->pickup_msg, 14, col(1, 0.9f, 0.7f, a));
    }

    /* ---------- announcements ---------- */
    if (g->announce_t > 0) {
        float a = clampf(g->announce_t, 0, 1);
        float sc = 1.0f + (1.0f - clampf(g->announce_t, 0, 1)) * 0.05f;
        (void)sc;
        float tw = ui_text_width(ui, g->announce, 40);
        ui_rect(ui, cx - tw * 0.5f - 30, h * 0.27f - 10, tw + 60, 62, col(0.02f, 0.03f, 0.05f, 0.62f * a));
        ui_rect(ui, cx - tw * 0.5f - 30, h * 0.27f - 10, tw + 60, 2, col(g->announce_color.r, g->announce_color.g, g->announce_color.b, a));
        ui_rect(ui, cx - tw * 0.5f - 30, h * 0.27f + 50, tw + 60, 2, col(g->announce_color.r, g->announce_color.g, g->announce_color.b, a));
        ui_text_center(ui, cx + 2, h * 0.27f + 2, g->announce, 40, col(0, 0, 0, a * 0.7f));
        ui_text_center(ui, cx, h * 0.27f, g->announce, 40, col(g->announce_color.r, g->announce_color.g, g->announce_color.b, a));
    }

    /* ---------- death overlay ---------- */
    if (!me->alive) {
        ui_rect(ui, 0, 0, (float)w, (float)h, col(0.1f, 0.01f, 0.01f, 0.25f));
        ui_text_center(ui, cx, h * 0.34f, "YOU WERE KILLED", 34, col(1.0f, 0.35f, 0.28f, 0.95f));
        const char* kname = "the arena";
        if (me->killer >= 0 && me->killer < g->num_players) kname = g->players[me->killer].name;
        char kb[96]; snprintf(kb, sizeof(kb), "by %s", kname);
        ui_text_center(ui, cx, h * 0.34f + 44, kb, 18, col(0.9f, 0.85f, 0.85f, 0.9f));
        char rb[64]; snprintf(rb, sizeof(rb), "RESPAWN IN %.1f", fmaxf(me->respawn_timer, 0));
        ui_text_center(ui, cx, h * 0.34f + 78, rb, 13, dim);
        ui_text_center(ui, cx, h * 0.34f + 100, "ENTER = SOFORT RESPAWN", 11, col(0.98f, 0.62f, 0.16f, 0.9f));
    }

    /* ---------- match over / victory & defeat sequence ---------- */
    if (g->match_over) {
        float t = g->match_over_t;
        float a = clampf(t * 2.4f, 0, 1);
        bool win = false;
        if (g->game_mode == GM_INVASION) win = g->invasion_victory;
        else if (game_mode_team_based(g->game_mode))
            win = (g->winner >= 0 && g->winner < g->num_players && me->team >= 0 &&
                   g->players[g->winner].team == me->team);
        else win = (g->winner == g->local_player);
        ui_rect(ui, 0, 0, (float)w, (float)h, col(0.0f, 0.0f, 0.0f, 0.70f * a));
        float pw = 760, ph = 486;
        float px = cx - pw * 0.5f, py = h * 0.5f - ph * 0.5f + (1.0f - a) * 40.0f;
        Color steel = col(0.075f, 0.082f, 0.092f, 0.97f);
        Color rust = col(0.42f, 0.18f, 0.075f, 1.0f);
        Color hazard = win ? col(0.96f, 0.74f, 0.16f, 1.0f) : col(0.82f, 0.22f, 0.14f, 1.0f);
        ui_rect(ui, px, py, pw, ph, col(0.012f, 0.014f, 0.017f, 0.96f));
        ui_rect(ui, px + 6, py + 6, pw - 12, ph - 12, steel);
        ui_rect_outline(ui, px + 6, py + 6, pw - 12, ph - 12, col(0.34f, 0.36f, 0.39f, 1), 2);
        ui_rect_outline(ui, px + 10, py + 10, pw - 20, ph - 20, rust, 1);
        /* hazard stripe rail */
        for (int i = 0; i < 24; i++)
            ui_rect(ui, px + 16 + i * 30, py + 18, 18, 9, col(hazard.r, hazard.g, hazard.b, 0.9f));
        for (int i = 0; i < 24; i++)
            ui_rect(ui, px + 16 + i * 30, py + ph - 27, 18, 9, col(hazard.r, hazard.g, hazard.b, 0.65f));
        const char* head = win ? "VICTORY" : (g->game_mode == GM_INVASION ? "OVERRUN" : "DEFEAT");
        const char* sub = g->result[0] ? g->result : (win ? "MATCH WON" : "MATCH LOST");
        ui_text_center(ui, cx + 3, py + 54, head, 62, col(0, 0, 0, 0.85f));
        ui_text_center(ui, cx, py + 51, head, 62, hazard);
        ui_text_center(ui, cx, py + 120, sub, 16, col(0.94f, 0.92f, 0.86f, a));
        ui_rect(ui, px + 40, py + 148, pw - 80, 2, rust);
        /* scoreboard top 5 with points */
        ScoreEntry sc[MAX_PLAYERS];
        int sn = 0;
        game_sort_scores(g, sc, &sn);
        int shown = sn > 5 ? 5 : sn;
        ui_text(ui, px + 46, py + 160, "RANGLISTE", 10, col(0.72f, 0.66f, 0.58f, 1));
        ui_text(ui, px + pw - 232, py + 160, "PUNKTE", 9, col(0.72f, 0.66f, 0.58f, 1));
        ui_text(ui, px + pw - 158, py + 160, "KILLS", 9, col(0.72f, 0.66f, 0.58f, 1));
        float ry = py + 178;
        for (int i = 0; i < shown; i++) {
            ScoreEntry* e = &sc[i];
            bool local = e->id == g->local_player;
            if (local) ui_rect(ui, px + 40, ry - 3, pw - 80, 26, col(0.16f, 0.13f, 0.08f, 0.9f));
            char nb[64];
            snprintf(nb, sizeof(nb), "%d. %s", i + 1, e->name);
            ui_text(ui, px + 50, ry, nb, 13, local ? hazard : col(0.88f, 0.89f, 0.9f, 1));
            char vb[24];
            snprintf(vb, sizeof(vb), "%d", e->points);
            ui_text_right(ui, px + pw - 198, ry, vb, 13, col(0.95f, 0.82f, 0.42f, 1));
            snprintf(vb, sizeof(vb), "%d", e->frags);
            ui_text_right(ui, px + pw - 124, ry, vb, 13, white);
            ry += 27;
        }
        /* reward ledger */
        float ly = py + 336;
        ui_rect(ui, px + 40, ly - 8, pw - 80, 84, col(0.02f, 0.03f, 0.035f, 0.85f));
        ui_rect(ui, px + 40, ly - 8, 4, 84, hazard);
        ui_text(ui, px + 56, ly, "AUSBEUTE", 10, col(0.72f, 0.66f, 0.58f, 1));
        char rb[192];
        snprintf(rb, sizeof(rb), "%d PUNKTE    //    %d CREDITS    //    %d KILLS    //    %d ASSISTS",
                 me->score_points, me->credits, me->frags, me->assists);
        ui_text(ui, px + 56, ly + 18, rb, 14, col(0.97f, 0.95f, 0.88f, 1));
        char rk[192];
        snprintf(rk, sizeof(rk), "KARRIERE %d PTS   //   RANG %s   //   %d/%d WELLEN   //   %d ZOMBIES",
                 me->career_points, game_rank_name(me->rank), g->invasion_wave, g->invasion_wave_max,
                 g->invasion_killed);
        ui_text(ui, px + 56, ly + 42, rk, 10, col(0.62f, 0.68f, 0.74f, 1));
        ui_text(ui, px + 56, ly + 62, win ? "DRUCK AUF DIE SCHULTER. DAS NAECHSTE GEFECHt KOMMT." : "ANALYSIERE. AUFRUESTEN. ZURUECK IN DIE ARENA.",
                9, col(0.55f, 0.50f, 0.44f, 1));
        if (t > 1.15f)
            ui_text_center(ui, cx, py + ph - 48, "TAB = SCORES   //   F5 = NEUSTART   //   F10 = MENUE   //   KONSOLE: results",
                           10, col(0.75f, 0.72f, 0.64f, a));
    }

    /* ---------- control hint ---------- */
    if (me->alive) {
        const char* hint = g->game_mode == GM_ALTIS
            ? "E = BENUTZEN / SAMMELN   B = BAUEN   R = NACHLADEN   ENTER = RESPAWN / UNSTUCK"
            : "B = BAUEN   E = SAMMELN   R = NACHLADEN   ENTER = RESPAWN / UNSTUCK";
        ui_text(ui, 22, (float)h - 38, hint, 9, col(0.42f, 0.48f, 0.56f, 0.75f));
    }

    /* ---------- fps / net ---------- */
    char fb[96];
    if (g->networked) snprintf(fb, sizeof(fb), "%.0f FPS   PING %.0f ms   NET %s", g->fps, g->ping, g->server_mode ? "HOST" : "CLIENT");
    else snprintf(fb, sizeof(fb), "%.0f FPS   DEMO", g->fps);
    if (show_fps || g->networked)
        ui_text(ui, 22, (float)h - 22, fb, 10, col(0.55f, 0.60f, 0.68f, 0.8f));
    if (g->demo_mode) {
        ui_text_right(ui, w - 22, 68, "DEMO // BOT MATCH", 11, col(0.75f, 0.8f, 0.9f, 0.75f));
    }
}

void hud_scoreboard(Ui* ui, Game* g, int w, int h) {
    ScoreEntry scores[MAX_PLAYERS];
    int n = 0;
    game_sort_scores(g, scores, &n);
    int shown = n > 16 ? 16 : n;
    float pw = 760, ph = 100 + shown * 34 + 40;
    float px = w * 0.5f - pw * 0.5f, py = h * 0.5f - ph * 0.5f;
    ui_rect(ui, 0, 0, (float)w, (float)h, col(0, 0, 0, 0.35f));
    ui_rect(ui, px, py, pw, ph, col(0.025f, 0.035f, 0.05f, 0.96f));
    ui_rect_outline(ui, px, py, pw, ph, col(0.22f, 0.28f, 0.36f, 1), 1);
    ui_rect(ui, px, py, pw, 58, col(0.07f, 0.10f, 0.14f, 1));
    ui_rect(ui, px, py + 55, pw, 3, accent);
    ui_text(ui, px + 22, py + 10, game_mode_name(g->game_mode), 20, white);
    char sub[192];
    if (game_mode_team_based(g->game_mode))
        snprintf(sub, sizeof(sub), "%s   //   RED %d : %d BLUE   //   LIMIT %d   //   %.0f FPS",
                 g->world->name, g->team_score[0], g->team_score[1], (int)g->score_limit, g->fps);
    else
        snprintf(sub, sizeof(sub), "%s   //   LIMIT %d   //   %.0f FPS", g->world->name, (int)g->score_limit, g->fps);
    ui_text(ui, px + 22, py + 34, sub, 10, dim);

    float ry = py + 68;
    ui_text(ui, px + 24, ry, "PLAYER", 10, dim);
    ui_text(ui, px + pw - 300, ry, "PTS", 10, dim);
    ui_text(ui, px + pw - 230, ry, "FRAGS", 10, dim);
    ui_text(ui, px + pw - 150, ry, "DEATHS", 10, dim);
    ui_text(ui, px + pw - 70, ry, "RATIO", 10, dim);
    ry += 18;
    ui_rect(ui, px + 18, ry, pw - 36, 1, col(0.2f, 0.24f, 0.3f, 1));
    ry += 8;
    for (int i = 0; i < shown; i++) {
        ScoreEntry* e = &scores[i];
        bool local = (e->id == g->local_player);
        Color bg = local ? col(0.10f, 0.18f, 0.27f, 0.95f) : (i % 2 ? col(0.055f, 0.065f, 0.08f, 0.9f) : col(0.035f, 0.045f, 0.06f, 0.9f));
        ui_rect(ui, px + 18, ry - 4, pw - 36, 30, bg);
        if (local) ui_rect(ui, px + 18, ry - 4, 3, 30, accent);
        ui_rect(ui, px + 28, ry + 3, 16, 16, e->color);
        ui_text(ui, px + 54, ry, e->name, 14, local ? white : col(0.82f, 0.86f, 0.92f, 1));
        if (e->bot || local) {
            const char* tag = local ? "YOU" : "BOT";
            ui_text(ui, px + 54 + ui_text_width(ui, e->name, 14) + 8, ry + 3, tag, 9, local ? accent : dim);
        }
        char v[24];
        snprintf(v, sizeof(v), "%d", e->points); ui_text_right(ui, px + pw - 300 + 40, ry, v, 12, col(0.95f, 0.82f, 0.42f, 1));
        snprintf(v, sizeof(v), "%d", e->frags); ui_text_right(ui, px + pw - 230 + 40, ry, v, 16, white);
        snprintf(v, sizeof(v), "%d", e->deaths); ui_text_right(ui, px + pw - 150 + 40, ry, v, 14, dim);
        float ratio = e->deaths > 0 ? (float)e->frags / e->deaths : (float)e->frags;
        snprintf(v, sizeof(v), "%.2f", ratio); ui_text_right(ui, px + pw - 70 + 30, ry, v, 13, dim);
        ry += 34;
    }
    if (n > shown) {
        char more[64]; snprintf(more, sizeof(more), "+ %d more players (console: players)", n - shown);
        ui_text(ui, px + 24, py + ph - 48, more, 9, dim);
    }
    ui_text_center(ui, w * 0.5f, py + ph - 28, "TAB: SCORES   //   ESC: MENU   //   F5: RESTART", 10, dim);
}

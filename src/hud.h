/* Arena2004 - in-game HUD */
#ifndef HUD_H
#define HUD_H
#include "game.h"

void hud_draw(Ui* ui, Game* g, int w, int h);
void hud_scoreboard(Ui* ui, Game* g, int w, int h);
void hud_draw_weapon_bar(Ui* ui, Game* g, int w, int h);
void hud_set_fps_visible(bool visible);
void hud_set_crosshair_visible(bool visible);
void hud_set_netgraph_visible(bool visible);

#endif

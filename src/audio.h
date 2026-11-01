/* Arena2004 - procedural audio (SDL2 audio, no external assets) */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SFX_STEP=0, SFX_JUMP, SFX_DODGE, SFX_LAND,
    SFX_AR, SFX_SHOCK, SFX_ROCKET, SFX_FLAK, SFX_LIGHTNING,
    SFX_ALT_FIRE,
    SFX_EXPLODE, SFX_IMPACT, SFX_PICKUP_HEALTH, SFX_PICKUP_ARMOR,
    SFX_PICKUP_WEAPON, SFX_PICKUP_AMMO, SFX_JUMPPAD,
    SFX_HIT, SFX_HEADSHOT, SFX_DEATH, SFX_RESPAWN,
    SFX_UI_CLICK, SFX_UI_HOVER, SFX_ANNOUNCE, SFX_COUNT
} SfxId;

typedef enum {
    MUS_MENU=0, MUS_GAME, MUS_EDITOR, MUS_COUNT
} MusicId;

void audio_init(void);
void audio_shutdown(void);
void audio_set_master(float v);
void audio_set_sfx(float v);
void audio_set_music(float v);
void audio_play(SfxId id, float volume, float pan);
void audio_play_at(SfxId id, float volume, float x, float y, float z,
                   float lx, float ly, float lz, float lfx, float lfz);
void audio_music(MusicId id);
void audio_update(float dt);
bool audio_active(void);

#endif

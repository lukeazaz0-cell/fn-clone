// Procedurally synthesized sound effects (no asset files needed).
#pragma once
#include <map>

#include "raylib.h"

namespace client {

enum class Sfx { Rifle, Shotgun, Smg, Sniper, Pistol, Rocket, Explosion, Pickaxe, Build, Hit, HeadHit, Pickup, Chest, Hurt, Storm, Bus, Click, Eliminated, Victory, Count };

class AudioSystem {
public:
    bool init();
    void shutdown();
    void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
    float masterVolume = 0.7f;

private:
    bool ok_ = false;
    Sound sounds_[(int)Sfx::Count]{};
};

} // namespace client

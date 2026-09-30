// Procedurally synthesized sound effects (no asset files needed).
#pragma once
#include <map>
#include <string>

#include "raylib.h"

namespace client {

enum class Sfx { Rifle, Shotgun, Smg, Sniper, Pistol, Rocket, Explosion, Pickaxe, Build, Hit, HeadHit, Pickup, Chest, Hurt, Storm, Bus, Click, Eliminated, Victory,
                 Jump, Land, Footstep, WeaponSwitch, StructureBreak, Impact, Blaster, BlasterRepeater, Horn, Boost, Crash, Count };

// Looping ambience channels
enum class Loop { Engine = 0, Ambience, Vehicle, Hover, Roll, Count };

class AudioSystem {
public:
    bool init();
    void shutdown();
    // Plays a random variant of the effect (recorded CC0 samples when available, otherwise synthesized).
    void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
    // Start/stop/adjust a looping channel; call every frame with the wanted volume (0 = off).
    void loop(Loop l, float volume, float pitch = 1.0f);
    float masterVolume = 0.7f;

private:
    bool ok_ = false;
    static constexpr int MAX_VARIANTS = 4;
    Sound sounds_[(int)Sfx::Count][MAX_VARIANTS]{};
    int variants_[(int)Sfx::Count]{};
    Sound loops_[(int)Loop::Count]{};
    bool loopOk_[(int)Loop::Count]{};
    unsigned rng_ = 1234567;
};

} // namespace client

#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace client {

namespace {

const int RATE = 22050;

struct Synth {
    std::vector<float> s;
    uint32_t seed = 12345;
    float noise() {
        seed = seed * 1664525u + 1013904223u;
        return ((seed >> 9) & 0x7FFF) / 16384.0f - 1.0f;
    }
    void resize(float seconds) { s.assign((size_t)(seconds * RATE), 0.0f); }
    // Filtered noise burst with exponential decay
    void noiseBurst(float start, float dur, float amp, float decay, float lowpass) {
        float y = 0;
        size_t a = (size_t)(start * RATE), n = (size_t)(dur * RATE);
        for (size_t i = 0; i < n && a + i < s.size(); i++) {
            float t = i / (float)RATE;
            y += (noise() - y) * lowpass;
            s[a + i] += y * amp * std::exp(-t * decay);
        }
    }
    void tone(float start, float dur, float f0, float f1, float amp, float decay, bool square = false) {
        size_t a = (size_t)(start * RATE), n = (size_t)(dur * RATE);
        float ph = 0;
        for (size_t i = 0; i < n && a + i < s.size(); i++) {
            float t = i / (float)RATE;
            float f = f0 + (f1 - f0) * (t / dur);
            ph += f / RATE;
            float v = square ? (std::fmod(ph, 1.0f) < 0.5f ? 1.0f : -1.0f) : std::sin(ph * 6.2831853f);
            s[a + i] += v * amp * std::exp(-t * decay);
        }
    }
    Wave toWave() {
        Wave w{};
        w.frameCount = (unsigned)s.size();
        w.sampleRate = RATE;
        w.sampleSize = 16;
        w.channels = 1;
        short* d = (short*)MemAlloc((unsigned)(s.size() * sizeof(short)));
        for (size_t i = 0; i < s.size(); i++) {
            float v = std::tanh(s[i]);
            d[i] = (short)(v * 30000);
        }
        w.data = d;
        return w;
    }
};

Sound make(Synth& sy) {
    Wave w = sy.toWave();
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}

} // namespace

bool AudioSystem::init() {
    InitAudioDevice();
    ok_ = IsAudioDeviceReady();
    if (!ok_) return false;
    auto build = [&](Sfx id, auto fn) {
        Synth sy;
        fn(sy);
        sounds_[(int)id][0] = make(sy);
        variants_[(int)id] = 1;
    };
    build(Sfx::Rifle, [](Synth& s) { s.resize(0.25f); s.noiseBurst(0, 0.25f, 1.4f, 22, 0.5f); s.tone(0, 0.08f, 180, 60, 0.8f, 30); });
    build(Sfx::Shotgun, [](Synth& s) { s.resize(0.45f); s.noiseBurst(0, 0.45f, 1.8f, 11, 0.35f); s.tone(0, 0.15f, 110, 40, 1.0f, 18); });
    build(Sfx::Smg, [](Synth& s) { s.resize(0.15f); s.noiseBurst(0, 0.15f, 1.1f, 35, 0.6f); s.tone(0, 0.05f, 260, 120, 0.5f, 40); });
    build(Sfx::Sniper, [](Synth& s) { s.resize(0.8f); s.noiseBurst(0, 0.8f, 1.6f, 7, 0.4f); s.tone(0, 0.2f, 120, 35, 1.2f, 10); });
    build(Sfx::Pistol, [](Synth& s) { s.resize(0.2f); s.noiseBurst(0, 0.2f, 1.2f, 28, 0.55f); s.tone(0, 0.06f, 300, 100, 0.6f, 35); });
    build(Sfx::Rocket, [](Synth& s) { s.resize(0.6f); s.noiseBurst(0, 0.6f, 0.9f, 5, 0.2f); s.tone(0, 0.6f, 90, 200, 0.3f, 3); });
    build(Sfx::Explosion, [](Synth& s) { s.resize(1.2f); s.noiseBurst(0, 1.2f, 2.2f, 4, 0.12f); s.tone(0, 0.5f, 70, 25, 1.4f, 5); });
    build(Sfx::Pickaxe, [](Synth& s) { s.resize(0.2f); s.tone(0, 0.2f, 520, 380, 0.6f, 25); s.noiseBurst(0, 0.05f, 0.6f, 60, 0.7f); });
    build(Sfx::Build, [](Synth& s) { s.resize(0.18f); s.tone(0, 0.18f, 240, 420, 0.5f, 18, true); s.noiseBurst(0, 0.05f, 0.3f, 50, 0.5f); });
    build(Sfx::Hit, [](Synth& s) { s.resize(0.1f); s.tone(0, 0.1f, 1400, 1200, 0.5f, 40); });
    build(Sfx::HeadHit, [](Synth& s) { s.resize(0.16f); s.tone(0, 0.16f, 1900, 2300, 0.55f, 22); });
    build(Sfx::Pickup, [](Synth& s) { s.resize(0.15f); s.tone(0, 0.07f, 600, 800, 0.4f, 20); s.tone(0.07f, 0.08f, 900, 1100, 0.4f, 20); });
    build(Sfx::Chest, [](Synth& s) { s.resize(0.6f); for (int i = 0; i < 4; i++) s.tone(i * 0.1f, 0.2f, 700 + i * 180, 700 + i * 180, 0.3f, 10); });
    build(Sfx::Hurt, [](Synth& s) { s.resize(0.2f); s.noiseBurst(0, 0.2f, 0.8f, 18, 0.25f); s.tone(0, 0.2f, 160, 90, 0.5f, 15); });
    build(Sfx::Storm, [](Synth& s) { s.resize(0.5f); s.noiseBurst(0, 0.5f, 0.7f, 5, 0.08f); s.tone(0, 0.5f, 55, 50, 0.4f, 3); });
    build(Sfx::Bus, [](Synth& s) { s.resize(0.4f); s.tone(0, 0.4f, 440, 440, 0.3f, 4, true); s.tone(0.2f, 0.2f, 660, 660, 0.3f, 6, true); });
    build(Sfx::Click, [](Synth& s) { s.resize(0.05f); s.tone(0, 0.05f, 900, 700, 0.3f, 60); });
    build(Sfx::Eliminated, [](Synth& s) { s.resize(0.5f); s.tone(0, 0.25f, 800, 1000, 0.4f, 6); s.tone(0.18f, 0.3f, 1200, 1500, 0.4f, 6); });
    build(Sfx::Victory, [](Synth& s) {
        s.resize(2.0f);
        float notes[] = {523, 659, 784, 1047, 784, 1047};
        for (int i = 0; i < 6; i++) s.tone(i * 0.18f, 0.5f, notes[i], notes[i], 0.35f, 3, true);
    });
    // Synth fallbacks for the effects that normally come from recordings
    build(Sfx::Jump, [](Synth& s) { s.resize(0.15f); s.tone(0, 0.15f, 300, 500, 0.3f, 18); });
    build(Sfx::Land, [](Synth& s) { s.resize(0.15f); s.noiseBurst(0, 0.15f, 0.6f, 30, 0.2f); });
    build(Sfx::Footstep, [](Synth& s) { s.resize(0.08f); s.noiseBurst(0, 0.08f, 0.4f, 60, 0.3f); });
    build(Sfx::WeaponSwitch, [](Synth& s) { s.resize(0.1f); s.tone(0, 0.1f, 700, 500, 0.3f, 30); });
    build(Sfx::StructureBreak, [](Synth& s) { s.resize(0.4f); s.noiseBurst(0, 0.4f, 1.2f, 9, 0.3f); });
    build(Sfx::Impact, [](Synth& s) { s.resize(0.2f); s.noiseBurst(0, 0.2f, 0.9f, 20, 0.4f); });
    build(Sfx::Blaster, [](Synth& s) { s.resize(0.2f); s.tone(0, 0.2f, 900, 300, 0.5f, 18, true); });
    build(Sfx::BlasterRepeater, [](Synth& s) { s.resize(0.12f); s.tone(0, 0.12f, 1000, 400, 0.4f, 25, true); });
    build(Sfx::Horn, [](Synth& s) {
        s.resize(0.55f);
        for (int k = 0; k < 2; k++) {
            s.tone(k * 0.28f, 0.22f, 392, 392, 0.3f, 1.5f, true);
            s.tone(k * 0.28f, 0.22f, 494, 494, 0.25f, 1.5f, true);
        }
    });
    build(Sfx::Boost, [](Synth& s) {
        s.resize(0.7f);
        s.noiseBurst(0, 0.7f, 0.9f, 3.5f, 0.08f);
        s.noiseBurst(0, 0.35f, 0.5f, 7, 0.4f);
        s.tone(0, 0.6f, 140, 420, 0.35f, 3.0f);
    });
    build(Sfx::Crash, [](Synth& s) {
        s.resize(0.5f);
        s.noiseBurst(0, 0.5f, 1.6f, 9, 0.25f);
        s.tone(0, 0.25f, 90, 45, 1.0f, 12);
        s.noiseBurst(0.04f, 0.2f, 0.6f, 20, 0.8f);
    });

    // Recorded CC0 samples (Kenney) replace the synth versions when present.
    std::string dir = std::string(GetApplicationDirectory()) + "assets/sounds/";
    auto file = [&](Sfx id, std::initializer_list<const char*> names) {
        int n = 0;
        Sound loaded[MAX_VARIANTS];
        for (const char* nm : names) {
            std::string path = dir + nm;
            if (n >= MAX_VARIANTS || !FileExists(path.c_str())) continue;
            Sound snd = LoadSound(path.c_str());
            if (snd.frameCount > 0) loaded[n++] = snd;
        }
        if (n == 0) return;
        for (int i = 0; i < variants_[(int)id]; i++) UnloadSound(sounds_[(int)id][i]);
        for (int i = 0; i < n; i++) sounds_[(int)id][i] = loaded[i];
        variants_[(int)id] = n;
    };
    file(Sfx::Jump, {"jump_a.ogg", "jump_b.ogg", "jump_c.ogg"});
    file(Sfx::Land, {"land.ogg"});
    file(Sfx::Footstep, {"walking.ogg"});
    file(Sfx::WeaponSwitch, {"weapon_change.ogg"});
    file(Sfx::Build, {"placement-a.ogg", "placement-b.ogg", "placement-c.ogg", "placement-d.ogg"});
    file(Sfx::StructureBreak, {"removal-a.ogg", "removal-b.ogg", "removal-c.ogg", "removal-d.ogg"});
    file(Sfx::Pickaxe, {"break.ogg"});
    file(Sfx::Pickup, {"coin.ogg"});
    file(Sfx::Impact, {"impact.ogg"});
    file(Sfx::Blaster, {"blaster.ogg"});
    file(Sfx::BlasterRepeater, {"blaster_repeater.ogg"});
    file(Sfx::Eliminated, {"enemy_destroy.ogg"});
    auto loopFile = [&](Loop l, const char* nm) {
        std::string path = dir + nm;
        if (!FileExists(path.c_str())) return;
        loops_[(int)l] = LoadSound(path.c_str());
        loopOk_[(int)l] = loops_[(int)l].frameCount > 0;
    };
    loopFile(Loop::Engine, "engine.ogg");
    loopFile(Loop::Ambience, "ambience.ogg");
    loopFile(Loop::Vehicle, "engine.ogg");
    // Synthesized loops: hoverboard hum and wheels rolling over ground
    auto synthLoop = [&](Loop l, auto fn) {
        Synth sy;
        fn(sy);
        loops_[(int)l] = make(sy);
        loopOk_[(int)l] = true;
    };
    synthLoop(Loop::Hover, [](Synth& s) {
        s.resize(1.0f);
        for (size_t i = 0; i < s.s.size(); i++) {
            float t = i / (float)RATE;
            s.s[i] = 0.28f * std::sin(t * 2 * 3.14159265f * 110) + 0.14f * std::sin(t * 2 * 3.14159265f * 220 + std::sin(t * 12.566f) * 0.8f) +
                     0.06f * std::sin(t * 2 * 3.14159265f * 330);
        }
    });
    synthLoop(Loop::Roll, [](Synth& s) {
        s.resize(1.0f);
        float y = 0;
        for (size_t i = 0; i < s.s.size(); i++) {
            y += (s.noise() - y) * 0.05f;
            float t = i / (float)RATE;
            s.s[i] = y * 1.4f * (0.8f + 0.2f * std::sin(t * 2 * 3.14159265f * 6));
        }
    });
    return true;
}

void AudioSystem::shutdown() {
    if (!ok_) return;
    for (int s = 0; s < (int)Sfx::Count; s++)
        for (int i = 0; i < variants_[s]; i++) UnloadSound(sounds_[s][i]);
    for (int l = 0; l < (int)Loop::Count; l++)
        if (loopOk_[l]) UnloadSound(loops_[l]);
    CloseAudioDevice();
    ok_ = false;
}

void AudioSystem::play(Sfx s, float volume, float pitch) {
    if (!ok_ || variants_[(int)s] == 0) return;
    float v = volume * masterVolume;
    if (v <= 0.01f) return;
    rng_ = rng_ * 1664525u + 1013904223u;
    int idx = (int)((rng_ >> 16) % (unsigned)variants_[(int)s]);
    Sound& snd = sounds_[(int)s][idx];
    // Slight random pitch so repeated sounds don't feel robotic
    float jitter = 0.94f + ((rng_ >> 8) % 1000) / 1000.0f * 0.12f;
    SetSoundVolume(snd, std::min(1.0f, v));
    SetSoundPitch(snd, pitch * jitter);
    PlaySound(snd);
}

void AudioSystem::loop(Loop l, float volume, float pitch) {
    if (!ok_ || !loopOk_[(int)l]) return;
    Sound& snd = loops_[(int)l];
    float v = volume * masterVolume;
    if (v <= 0.01f) {
        if (IsSoundPlaying(snd)) StopSound(snd);
        return;
    }
    SetSoundVolume(snd, std::min(1.0f, v));
    SetSoundPitch(snd, pitch);
    if (!IsSoundPlaying(snd)) PlaySound(snd);
}

} // namespace client

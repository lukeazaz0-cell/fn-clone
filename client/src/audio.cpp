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
        sounds_[(int)id] = make(sy);
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
    return true;
}

void AudioSystem::shutdown() {
    if (!ok_) return;
    for (auto& s : sounds_) UnloadSound(s);
    CloseAudioDevice();
    ok_ = false;
}

void AudioSystem::play(Sfx s, float volume, float pitch) {
    if (!ok_) return;
    Sound& snd = sounds_[(int)s];
    float v = volume * masterVolume;
    if (v <= 0.01f) return;
    SetSoundVolume(snd, std::min(1.0f, v));
    SetSoundPitch(snd, pitch);
    PlaySound(snd);
}

} // namespace client

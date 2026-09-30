// 3D model files (CC0 Kenney assets, see client/assets/CREDITS.md). Every model is optional:
// if a file is missing the game falls back to its procedural geometry.
#pragma once
#include <string>

#include "raylib.h"
#include "render.h"

namespace client {

enum class HeldModel : uint8_t { Blaster = 0, BlasterRepeater, Trophy, Cloud, Count };

// Soldier animation clips we use (indices resolved by name at load time).
enum class SoldierAnim : uint8_t { Idle = 0, Walk, Sprint, Jump, Fall, Crouch, Die, Cheer, HoldShoot, Hold, Count };

class ModelLibrary {
public:
    bool load(Lighting& light);
    // Point every loaded model at a different lit shader (lobby preview vs. in-game renderer).
    void useShader(Shader s);
    void unload();

    bool has(PropModel m) const;
    void drawProp(PropModel m, const si::Vec3& pos, float yaw, float scale, Shader* override = nullptr);

    bool hasHeld(HeldModel m) const;
    // Draws a held/pickup model using a character-space basis (b.f = barrel direction).
    void drawHeld(HeldModel m, const si::Vec3& pos, const Basis& b, float scale, Shader* override = nullptr);

    bool hasSoldier() const { return soldierOk_; }
    // Poses the soldier for one character and draws it. `time` drives the clip.
    void drawSoldier(const si::Vec3& feet, float yaw, SoldierAnim anim, float time, float scale, Shader* override = nullptr,
                     const Basis* orient = nullptr);

    std::string assetDir;

private:
    struct Entry {
        Model model{};
        bool ok = false;
        float yOffset = 0;
    };
    Entry props_[(int)PropModel::Count];
    Entry held_[(int)HeldModel::Count];
    Model soldier_{};
    ModelAnimation* anims_ = nullptr;
    int animCount_ = 0;
    int clip_[(int)SoldierAnim::Count];
    bool soldierOk_ = false;
    Shader shader_{};

    Entry loadEntry(const std::string& rel);
    void drawWith(Model& m, const Matrix& transform, Shader* override);
    void applyCloudShader();
};

// Global access for renderers (set by GameClient / App).
ModelLibrary* models();
void setModels(ModelLibrary* lib);

} // namespace client

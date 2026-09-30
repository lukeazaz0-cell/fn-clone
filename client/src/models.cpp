#include "models.h"

#include <cmath>
#include <cstring>

#include "raymath.h"
#include "rlgl.h"

namespace client {

static ModelLibrary* gModels = nullptr;
ModelLibrary* models() { return gModels; }
void setModels(ModelLibrary* lib) { gModels = lib; }

namespace {

const char* kPropFiles[(int)PropModel::Count] = {
    nullptr,
    "models/racing/vehicle-truck-green.glb",
    "models/racing/vehicle-truck-purple.glb",
    "models/racing/vehicle-truck-red.glb",
    "models/racing/vehicle-truck-yellow.glb",
    "models/racing/vehicle-motorcycle.glb",
    "models/arena/statue.glb",
    "models/arena/column.glb",
    "models/arena/banner.glb",
    "models/arena/weapon-rack.glb",
    "models/city/pavement-fountain.glb",
    "models/platformer/flag.glb",
    "models/arena/tree.glb",
    "models/city/grass-trees-tall.glb",
};

const char* kHeldFiles[(int)HeldModel::Count] = {
    "models/fps/blaster.glb",
    "models/fps/blaster-repeater.glb",
    "models/arena/trophy.glb",
    "models/fps/cloud.glb",
};

const char* kClipNames[(int)SoldierAnim::Count] = {"idle", "walk", "sprint", "jump", "fall", "crouch", "die", "emote-yes",
                                                     "holding-right-shoot", "holding-right"};

Matrix basisMatrix(const Basis& b0, const si::Vec3& pos, float scale) {
    // Columns: right, up, forward. Mirror a left-handed basis so geometry isn't flipped inside out.
    Basis b = b0;
    if (b0.r.cross(b0.u).dot(b0.f) < 0) b.r = b0.r * -1.0f;
    Matrix m{};
    m.m0 = b.r.x * scale; m.m4 = b.u.x * scale; m.m8 = b.f.x * scale;  m.m12 = pos.x;
    m.m1 = b.r.y * scale; m.m5 = b.u.y * scale; m.m9 = b.f.y * scale;  m.m13 = pos.y;
    m.m2 = b.r.z * scale; m.m6 = b.u.z * scale; m.m10 = b.f.z * scale; m.m14 = pos.z;
    m.m15 = 1.0f;
    return m;
}

} // namespace

ModelLibrary::Entry ModelLibrary::loadEntry(const std::string& rel) {
    Entry e;
    std::string path = assetDir + rel;
    if (!FileExists(path.c_str())) return e;
    e.model = LoadModel(path.c_str());
    if (e.model.meshCount == 0) return e;
    for (int i = 0; i < e.model.materialCount; i++) {
        e.model.materials[i].shader = shader_;
        Texture2D& t = e.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
        if (t.id && t.id != rlGetTextureIdDefault()) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    BoundingBox bb = GetModelBoundingBox(e.model);
    e.yOffset = -bb.min.y;
    e.ok = true;
    return e;
}

bool ModelLibrary::load(Lighting& light) {
    shader_ = light.model;
    if (assetDir.empty()) assetDir = std::string(GetApplicationDirectory()) + "assets/";
    int loaded = 0;
    for (int i = 1; i < (int)PropModel::Count; i++) {
        props_[i] = loadEntry(kPropFiles[i]);
        loaded += props_[i].ok;
    }
    for (int i = 0; i < (int)HeldModel::Count; i++) {
        held_[i] = loadEntry(kHeldFiles[i]);
        loaded += held_[i].ok;
    }
    applyCloudShader();
    std::string sp = assetDir + "models/arena/character-soldier.glb";
    if (FileExists(sp.c_str())) {
        soldier_ = LoadModel(sp.c_str());
        for (int i = 0; i < soldier_.materialCount; i++) soldier_.materials[i].shader = shader_;
        anims_ = LoadModelAnimations(sp.c_str(), &animCount_);
        for (int c = 0; c < (int)SoldierAnim::Count; c++) {
            clip_[c] = -1;
            for (int a = 0; a < animCount_; a++)
                if (std::strcmp(anims_[a].name, kClipNames[c]) == 0) clip_[c] = a;
        }
        soldierOk_ = soldier_.meshCount > 0 && anims_ && clip_[(int)SoldierAnim::Idle] >= 0;
        loaded += soldierOk_;
    }
    TraceLog(LOG_INFO, "ASSETS: loaded %d model files from %s", loaded, assetDir.c_str());
    return loaded > 0;
}

void ModelLibrary::unload() {
    for (auto& e : props_) if (e.ok) UnloadModel(e.model);
    for (auto& e : held_) if (e.ok) UnloadModel(e.model);
    if (anims_) UnloadModelAnimations(anims_, animCount_);
    if (soldier_.meshCount) UnloadModel(soldier_);
    *this = ModelLibrary();
}

void ModelLibrary::useShader(Shader s) {
    shader_ = s;
    auto apply = [&](Model& m) { for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = s; };
    for (auto& e : props_) if (e.ok) apply(e.model);
    for (auto& e : held_) if (e.ok) apply(e.model);
    if (soldier_.meshCount) apply(soldier_);
    applyCloudShader();
}

// Clouds are drawn unlit (bright, flat) so their undersides don't turn grey.
void ModelLibrary::applyCloudShader() {
    Entry& c = held_[(int)HeldModel::Cloud];
    if (!c.ok) return;
    Shader def{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
    for (int i = 0; i < c.model.materialCount; i++) c.model.materials[i].shader = def;
}

bool ModelLibrary::has(PropModel m) const { return (int)m > 0 && (int)m < (int)PropModel::Count && props_[(int)m].ok; }
bool ModelLibrary::hasHeld(HeldModel m) const { return held_[(int)m].ok; }

void ModelLibrary::drawWith(Model& m, const Matrix& transform, Shader* override) {
    Matrix keep = m.transform;
    m.transform = transform;
    if (override) {
        Shader keepShader = m.materialCount ? m.materials[0].shader : shader_;
        for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = *override;
        DrawModel(m, {0, 0, 0}, 1.0f, WHITE);
        for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = keepShader;
    } else {
        DrawModel(m, {0, 0, 0}, 1.0f, WHITE);
    }
    m.transform = keep;
}

void ModelLibrary::drawProp(PropModel pm, const si::Vec3& pos, float yaw, float scale, Shader* override) {
    if (!has(pm)) return;
    Entry& e = props_[(int)pm];
    Basis b = yawBasis(yaw);
    drawWith(e.model, basisMatrix(b, pos + si::Vec3{0, e.yOffset * scale, 0}, scale), override);
}

void ModelLibrary::drawHeld(HeldModel hm, const si::Vec3& pos, const Basis& b, float scale, Shader* override) {
    if (!hasHeld(hm)) return;
    drawWith(held_[(int)hm].model, basisMatrix(b, pos, scale), override);
}

void ModelLibrary::drawSoldier(const si::Vec3& feet, float yaw, SoldierAnim anim, float time, float scale, Shader* override) {
    if (!soldierOk_) return;
    int clip = clip_[(int)anim];
    if (clip < 0) clip = clip_[(int)SoldierAnim::Idle];
    const ModelAnimation& a = anims_[clip];
    // Clips are authored at 24 fps; "die" holds its last frame.
    int frame = (int)(time * 24.0f);
    frame = anim == SoldierAnim::Die ? std::min(frame, a.frameCount - 1) : frame % std::max(1, a.frameCount);
    UpdateModelAnimation(soldier_, a, frame);
    drawWith(soldier_, basisMatrix(yawBasis(yaw), feet, scale), override);
}

} // namespace client

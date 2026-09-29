// Cosmetic catalog. All outfits here are original designs made for this project.
// The backend seeds its catalog from this table so client, server and backend agree.
#pragma once
#include <string>
#include <vector>

#include "../common/math.h"
#include "defs.h"

namespace si {

enum class CosmeticType : uint8_t { Outfit = 0, BackBling, Pickaxe, Glider, Contrail, Emote, Count };
constexpr const char* COSMETIC_TYPE_NAMES[] = {"outfit", "backbling", "pickaxe", "glider", "contrail", "emote"};

// Visual style knobs used by the procedural character renderer.
struct CosmeticStyle {
    Color4 primary, secondary, accent, skin, hair;
    uint8_t shape = 0;   // outfit: 0 none,1 helmet,2 hood,3 cap,4 visor ; backbling: shape id ; emote: animation id
    uint8_t pattern = 0; // outfit: 0 plain,1 stripes,2 split
};

struct CosmeticDef {
    std::string id;
    std::string name;
    CosmeticType type;
    Rarity rarity;
    int price;          // coins, 0 = default item every account owns
    int unlockLevel;    // >0 = unlocked by reaching this level (season pass style)
    CosmeticStyle style;
};

const std::vector<CosmeticDef>& cosmeticCatalog();
const CosmeticDef* findCosmetic(const std::string& id);
// Returns the default id for a slot type.
std::string defaultCosmetic(CosmeticType t);

// Equipped loadout. Emotes: 6 wheel slots.
struct Loadout {
    std::string outfit = "outfit_recruit_a";
    std::string backbling = "bb_none";
    std::string pickaxe = "pick_default";
    std::string glider = "glider_default";
    std::string contrail = "trail_none";
    std::string emotes[6] = {"emote_dance", "emote_wave", "emote_none", "emote_none", "emote_none", "emote_none"};
};

} // namespace si

#include "cosmetics.h"

namespace si {

namespace {

CosmeticStyle st(Color4 p, Color4 s, Color4 a, Color4 skin, Color4 hair, uint8_t shape = 0, uint8_t pattern = 0) {
    CosmeticStyle c;
    c.primary = p; c.secondary = s; c.accent = a; c.skin = skin; c.hair = hair; c.shape = shape; c.pattern = pattern;
    return c;
}

std::vector<CosmeticDef> build() {
    std::vector<CosmeticDef> v;
    const Color4 tone1 = rgb(241, 194, 160), tone2 = rgb(198, 134, 94), tone3 = rgb(120, 78, 52), tone4 = rgb(255, 219, 184);
    auto add = [&](const char* id, const char* name, CosmeticType t, Rarity r, int price, int lvl, CosmeticStyle s) {
        v.push_back(CosmeticDef{id, name, t, r, price, lvl, s});
    };
    using CT = CosmeticType;
    using R = Rarity;
    // Outfits (defaults)
    add("outfit_recruit_a", "Recruit Alpha", CT::Outfit, R::Common, 0, 0, st(rgb(70, 90, 140), rgb(50, 50, 60), rgb(200, 200, 210), tone1, rgb(90, 60, 30)));
    add("outfit_recruit_b", "Recruit Bravo", CT::Outfit, R::Common, 0, 0, st(rgb(140, 80, 60), rgb(60, 60, 50), rgb(220, 200, 150), tone3, rgb(20, 20, 20)));
    add("outfit_recruit_c", "Recruit Charlie", CT::Outfit, R::Common, 0, 0, st(rgb(80, 130, 80), rgb(70, 60, 50), rgb(230, 230, 230), tone2, rgb(200, 160, 60)));
    // Model-based outfit (CC0 rigged soldier by Kenney); shape 10 = draw the soldier model file
    add("outfit_arena_trooper", "Arena Trooper", CT::Outfit, R::Rare, 0, 0, st(rgb(200, 90, 60), rgb(90, 90, 100), rgb(240, 200, 80), tone1, rgb(60, 40, 30), 10));
    // Shop / level outfits (original characters)
    add("outfit_ember_ranger", "Ember Ranger", CT::Outfit, R::Rare, 1200, 0, st(rgb(200, 70, 30), rgb(60, 30, 20), rgb(255, 190, 60), tone2, rgb(150, 40, 20), 2, 1));
    add("outfit_tidecaller", "Tidecaller", CT::Outfit, R::Epic, 1500, 0, st(rgb(30, 120, 170), rgb(20, 60, 90), rgb(120, 230, 230), tone4, rgb(20, 90, 110), 4, 2));
    add("outfit_nightjar", "Nightjar", CT::Outfit, R::Legendary, 2000, 0, st(rgb(30, 30, 45), rgb(90, 40, 140), rgb(200, 120, 255), tone3, rgb(10, 10, 20), 1, 2));
    add("outfit_cactus_jack", "Prickly Pete", CT::Outfit, R::Uncommon, 800, 0, st(rgb(80, 160, 70), rgb(230, 200, 120), rgb(250, 120, 160), rgb(80, 160, 70), rgb(80, 160, 70), 3, 1));
    add("outfit_circuit", "Circuit Breaker", CT::Outfit, R::Epic, 1500, 0, st(rgb(40, 50, 60), rgb(20, 220, 180), rgb(250, 250, 90), tone1, rgb(30, 30, 30), 4, 1));
    add("outfit_frostbyte", "Frostbyte", CT::Outfit, R::Rare, 0, 10, st(rgb(220, 240, 255), rgb(90, 150, 220), rgb(40, 60, 120), tone4, rgb(240, 240, 255), 2, 0));
    add("outfit_dune_runner", "Dune Runner", CT::Outfit, R::Uncommon, 0, 5, st(rgb(210, 180, 120), rgb(120, 90, 60), rgb(200, 60, 40), tone2, rgb(60, 40, 20), 3, 0));
    add("outfit_magma_knight", "Magma Knight", CT::Outfit, R::Legendary, 0, 25, st(rgb(50, 40, 40), rgb(240, 90, 20), rgb(255, 210, 60), tone3, rgb(30, 20, 20), 1, 1));
    add("outfit_blossom", "Petal Guard", CT::Outfit, R::Epic, 0, 15, st(rgb(250, 170, 200), rgb(250, 250, 250), rgb(200, 60, 110), tone4, rgb(230, 100, 150), 0, 2));
    add("outfit_skyline", "Skyline Courier", CT::Outfit, R::Rare, 1200, 0, st(rgb(240, 200, 40), rgb(40, 40, 40), rgb(230, 230, 230), tone1, rgb(40, 30, 20), 3, 1));
    add("outfit_verdant", "Verdant Scout", CT::Outfit, R::Uncommon, 800, 0, st(rgb(60, 100, 50), rgb(100, 80, 50), rgb(200, 180, 90), tone2, rgb(80, 50, 30), 2, 0));

    // Back blings: shape 0 none, 1 backpack, 2 cape, 3 wings, 4 tank, 5 shield
    add("bb_none", "No Back Bling", CT::BackBling, R::Common, 0, 0, st(rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0), tone1, tone1, 0));
    add("bb_satchel", "Trail Satchel", CT::BackBling, R::Uncommon, 300, 0, st(rgb(120, 90, 50), rgb(80, 60, 30), rgb(200, 180, 120), tone1, tone1, 1));
    add("bb_cape_red", "Crimson Cape", CT::BackBling, R::Rare, 500, 0, st(rgb(180, 30, 40), rgb(120, 20, 30), rgb(250, 210, 80), tone1, tone1, 2));
    add("bb_wings", "Glimmer Wings", CT::BackBling, R::Epic, 0, 20, st(rgb(180, 220, 255), rgb(120, 170, 255), rgb(255, 255, 255), tone1, tone1, 3));
    add("bb_tank", "Coolant Tank", CT::BackBling, R::Rare, 500, 0, st(rgb(60, 70, 80), rgb(40, 220, 200), rgb(220, 220, 220), tone1, tone1, 4));
    add("bb_shield", "Oak Shield", CT::BackBling, R::Uncommon, 0, 3, st(rgb(140, 100, 60), rgb(180, 180, 190), rgb(200, 50, 40), tone1, tone1, 5));

    // Pickaxes: shape 0 classic, 1 axe, 2 hammer, 3 scythe
    add("pick_default", "Standard Pickaxe", CT::Pickaxe, R::Common, 0, 0, st(rgb(150, 150, 160), rgb(110, 70, 40), rgb(0, 0, 0), tone1, tone1, 0));
    add("pick_axe", "Timber Splitter", CT::Pickaxe, R::Uncommon, 500, 0, st(rgb(180, 180, 190), rgb(90, 60, 30), rgb(200, 40, 40), tone1, tone1, 1));
    add("pick_hammer", "Rock Knocker", CT::Pickaxe, R::Rare, 800, 0, st(rgb(90, 90, 100), rgb(50, 50, 55), rgb(250, 200, 40), tone1, tone1, 2));
    add("pick_scythe", "Crescent Reaper", CT::Pickaxe, R::Epic, 0, 12, st(rgb(200, 200, 240), rgb(40, 20, 60), rgb(160, 80, 255), tone1, tone1, 3));

    // Gliders
    add("glider_default", "Canopy", CT::Glider, R::Common, 0, 0, st(rgb(60, 120, 200), rgb(240, 240, 240), rgb(40, 40, 40), tone1, tone1, 0));
    add("glider_sunset", "Sunset Sail", CT::Glider, R::Rare, 800, 0, st(rgb(250, 120, 60), rgb(250, 200, 80), rgb(120, 40, 80), tone1, tone1, 0));
    add("glider_raven", "Midnight Kite", CT::Glider, R::Epic, 1200, 0, st(rgb(30, 30, 40), rgb(150, 50, 220), rgb(220, 220, 220), tone1, tone1, 0));
    add("glider_lime", "Lime Flyer", CT::Glider, R::Uncommon, 0, 7, st(rgb(150, 230, 60), rgb(40, 90, 40), rgb(250, 250, 250), tone1, tone1, 0));

    // Contrails
    add("trail_none", "Default Trail", CT::Contrail, R::Common, 0, 0, st(rgb(255, 255, 255), rgb(255, 255, 255), rgb(255, 255, 255), tone1, tone1, 0));
    add("trail_fire", "Flame Trail", CT::Contrail, R::Rare, 500, 0, st(rgb(255, 120, 30), rgb(255, 220, 60), rgb(200, 40, 20), tone1, tone1, 0));
    add("trail_sparkle", "Sparkle Trail", CT::Contrail, R::Epic, 0, 18, st(rgb(255, 180, 250), rgb(150, 220, 255), rgb(255, 255, 255), tone1, tone1, 0));

    // Emotes: shape = animation id
    add("emote_none", "Empty", CT::Emote, R::Common, 0, 0, st({}, {}, {}, tone1, tone1, 0));
    add("emote_dance", "Groove", CT::Emote, R::Common, 0, 0, st({}, {}, {}, tone1, tone1, 1));
    add("emote_wave", "Hello There", CT::Emote, R::Common, 0, 0, st({}, {}, {}, tone1, tone1, 2));
    add("emote_spin", "Spin Cycle", CT::Emote, R::Uncommon, 300, 0, st({}, {}, {}, tone1, tone1, 3));
    add("emote_jumpjack", "Jumping Jacks", CT::Emote, R::Uncommon, 0, 4, st({}, {}, {}, tone1, tone1, 4));
    add("emote_robot", "Circuit Shuffle", CT::Emote, R::Rare, 500, 0, st({}, {}, {}, tone1, tone1, 5));
    add("emote_flex", "Big Flex", CT::Emote, R::Epic, 0, 8, st({}, {}, {}, tone1, tone1, 6));
    return v;
}

} // namespace

const std::vector<CosmeticDef>& cosmeticCatalog() {
    static const std::vector<CosmeticDef> c = build();
    return c;
}

const CosmeticDef* findCosmetic(const std::string& id) {
    for (auto& c : cosmeticCatalog())
        if (c.id == id) return &c;
    return nullptr;
}

std::string defaultCosmetic(CosmeticType t) {
    switch (t) {
        case CosmeticType::Outfit: return "outfit_recruit_a";
        case CosmeticType::BackBling: return "bb_none";
        case CosmeticType::Pickaxe: return "pick_default";
        case CosmeticType::Glider: return "glider_default";
        case CosmeticType::Contrail: return "trail_none";
        case CosmeticType::Emote: return "emote_none";
        default: return "";
    }
}

} // namespace si

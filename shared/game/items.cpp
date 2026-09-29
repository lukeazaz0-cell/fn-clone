#include "items.h"

#include <array>

namespace si {

namespace {

std::array<ItemDef, (size_t)ItemType::Count> buildDefs() {
    std::array<ItemDef, (size_t)ItemType::Count> d{};
    auto gun = [&](ItemType t, const char* name, AmmoType ammo, Rarity lo, Rarity hi, WeaponStats w, Color4 c) {
        ItemDef& def = d[(size_t)t];
        def.type = t; def.name = name; def.cls = ItemClass::Gun; def.ammo = ammo;
        def.minRarity = lo; def.maxRarity = hi; def.weapon = w; def.color = c; def.maxStack = 1;
    };
    WeaponStats w;

    w = {}; w.damage = 30; w.fireInterval = 1.0f / 5.5f; w.magSize = 30; w.reloadTime = 2.3f; w.range = 250;
    w.falloffStart = 50; w.spreadHip = 0.045f; w.spreadAds = 0.012f; w.bloomPerShot = 0.006f; w.maxBloom = 0.05f;
    gun(ItemType::AssaultRifle, "Assault Rifle", AmmoType::Medium, Rarity::Common, Rarity::Legendary, w, rgb(70, 70, 78));

    w = {}; w.damage = 29; w.fireInterval = 0.55f; w.burst = 3; w.burstInterval = 0.07f; w.magSize = 30; w.reloadTime = 2.4f;
    w.range = 250; w.spreadHip = 0.04f; w.spreadAds = 0.008f; w.bloomPerShot = 0.004f; w.maxBloom = 0.04f; w.automatic = false;
    gun(ItemType::BurstRifle, "Burst Rifle", AmmoType::Medium, Rarity::Common, Rarity::Epic, w, rgb(90, 80, 60));

    w = {}; w.damage = 24; w.fireInterval = 1.0f / 3.5f; w.magSize = 20; w.reloadTime = 2.3f; w.range = 300;
    w.spreadHip = 0.05f; w.spreadAds = 0.002f; w.bloomPerShot = 0.004f; w.maxBloom = 0.05f; w.automatic = false; w.adsZoom = 2.5f;
    gun(ItemType::ScopedRifle, "Scoped Rifle", AmmoType::Medium, Rarity::Rare, Rarity::Epic, w, rgb(60, 70, 60));

    w = {}; w.damage = 9.5f; w.pellets = 10; w.headshotMul = 2.0f; w.fireInterval = 1.0f / 0.7f; w.magSize = 5; w.reloadTime = 4.8f;
    w.range = 40; w.falloffStart = 7; w.falloffMin = 0.35f; w.spreadHip = 0.09f; w.spreadAds = 0.075f; w.automatic = false;
    w.equipTime = 0.3f; w.structureMul = 0.8f;
    gun(ItemType::PumpShotgun, "Pump Shotgun", AmmoType::Shells, Rarity::Uncommon, Rarity::Legendary, w, rgb(120, 70, 40));

    w = {}; w.damage = 7.4f; w.pellets = 10; w.fireInterval = 1.0f / 1.5f; w.magSize = 8; w.reloadTime = 5.7f; w.range = 35;
    w.falloffStart = 7; w.falloffMin = 0.35f; w.spreadHip = 0.1f; w.spreadAds = 0.085f; w.automatic = false; w.structureMul = 0.8f;
    gun(ItemType::TacticalShotgun, "Tactical Shotgun", AmmoType::Shells, Rarity::Common, Rarity::Epic, w, rgb(50, 50, 50));

    w = {}; w.damage = 17; w.fireInterval = 1.0f / 12.0f; w.magSize = 30; w.reloadTime = 2.2f; w.range = 120; w.falloffStart = 25;
    w.falloffMin = 0.5f; w.spreadHip = 0.05f; w.spreadAds = 0.035f; w.bloomPerShot = 0.004f; w.maxBloom = 0.07f;
    gun(ItemType::SMG, "Submachine Gun", AmmoType::Light, Rarity::Common, Rarity::Epic, w, rgb(40, 40, 50));

    w = {}; w.damage = 24; w.fireInterval = 1.0f / 6.75f; w.magSize = 16; w.reloadTime = 1.4f; w.range = 150; w.falloffStart = 30;
    w.spreadHip = 0.03f; w.spreadAds = 0.012f; w.bloomPerShot = 0.012f; w.maxBloom = 0.06f; w.automatic = false; w.equipTime = 0.2f;
    gun(ItemType::Pistol, "Pistol", AmmoType::Light, Rarity::Common, Rarity::Rare, w, rgb(80, 80, 80));

    w = {}; w.damage = 105; w.headshotMul = 2.5f; w.fireInterval = 1.0f / 0.33f; w.magSize = 1; w.reloadTime = 2.7f; w.range = 700;
    w.falloffStart = 700; w.spreadHip = 0.12f; w.spreadAds = 0.0f; w.automatic = false; w.adsZoom = 4.5f; w.equipTime = 0.6f;
    gun(ItemType::SniperRifle, "Bolt Sniper", AmmoType::Heavy, Rarity::Rare, Rarity::Legendary, w, rgb(60, 50, 40));

    w = {}; w.damage = 85; w.fireInterval = 1.0f / 0.75f; w.magSize = 1; w.reloadTime = 3.0f; w.range = 400; w.projectile = true;
    w.projectileSpeed = 60; w.explosionRadius = 5.5f; w.structureMul = 4.0f; w.automatic = false; w.spreadHip = 0.005f;
    w.spreadAds = 0.0f; w.equipTime = 0.6f;
    gun(ItemType::RocketLauncher, "Rocket Launcher", AmmoType::Rockets, Rarity::Rare, Rarity::Legendary, w, rgb(60, 90, 60));

    w = {}; w.damage = 18; w.fireInterval = 1.0f / 12.0f; w.magSize = 999; w.reloadTime = 0; w.range = 200; w.spreadHip = 0.06f;
    w.spreadAds = 0.045f; w.structureMul = 1.5f; w.spinUp = 0.6f; w.bloomPerShot = 0.002f; w.maxBloom = 0.03f;
    gun(ItemType::Minigun, "Minigun", AmmoType::Light, Rarity::Epic, Rarity::Legendary, w, rgb(50, 50, 55));

    auto thr = [&](ItemType t, const char* name, float dmg, float radius, int stack, Color4 c) {
        ItemDef& def = d[(size_t)t];
        def.type = t; def.name = name; def.cls = ItemClass::Throwable; def.maxStack = stack; def.color = c;
        def.minRarity = def.maxRarity = t == ItemType::Grenade ? Rarity::Uncommon : Rarity::Rare;
        WeaponStats ws; ws.damage = dmg; ws.explosionRadius = radius; ws.projectile = true; ws.projectileSpeed = 24;
        ws.fireInterval = 0.8f; ws.automatic = false; ws.magSize = 1; ws.structureMul = 3.0f; ws.equipTime = 0.2f;
        def.weapon = ws;
    };
    thr(ItemType::Grenade, "Grenade", 100, 5.0f, 6, rgb(60, 110, 60));
    thr(ItemType::ImpulseGrenade, "Impulse Grenade", 0, 6.0f, 3, rgb(90, 110, 220));

    auto con = [&](ItemType t, const char* name, Rarity r, ConsumableStats cs, Color4 c) {
        ItemDef& def = d[(size_t)t];
        def.type = t; def.name = name; def.cls = ItemClass::Consumable; def.maxStack = cs.maxStack;
        def.minRarity = def.maxRarity = r; def.consumable = cs; def.color = c;
    };
    ConsumableStats cs;
    cs = {}; cs.useTime = 3.5f; cs.heal = 15; cs.healCap = 75; cs.maxStack = 15;
    con(ItemType::Bandages, "Bandages", Rarity::Common, cs, rgb(230, 230, 220));
    cs = {}; cs.useTime = 10.0f; cs.heal = 100; cs.healCap = 100; cs.maxStack = 3;
    con(ItemType::Medkit, "Medkit", Rarity::Uncommon, cs, rgb(230, 60, 60));
    cs = {}; cs.useTime = 2.0f; cs.shield = 25; cs.shieldCap = 50; cs.maxStack = 6;
    con(ItemType::SmallShield, "Small Shield", Rarity::Uncommon, cs, rgb(90, 170, 255));
    cs = {}; cs.useTime = 5.0f; cs.shield = 50; cs.shieldCap = 100; cs.maxStack = 3;
    con(ItemType::ShieldPotion, "Shield Potion", Rarity::Rare, cs, rgb(40, 110, 255));
    cs = {}; cs.useTime = 2.0f; cs.overTimeHeal = 1; cs.overTimeShield = 1; cs.overTimeDuration = 75; cs.maxStack = 2;
    con(ItemType::RegenSoda, "Regen Soda", Rarity::Epic, cs, rgb(110, 60, 220));
    cs = {}; cs.useTime = 15.0f; cs.heal = 100; cs.shield = 100; cs.maxStack = 1; cs.movementWhileUsing = false;
    con(ItemType::MegaFlask, "Mega Flask", Rarity::Legendary, cs, rgb(60, 140, 255));

    {
        ItemDef& def = d[(size_t)ItemType::LaunchPad];
        def.type = ItemType::LaunchPad; def.name = "Launch Pad"; def.cls = ItemClass::Utility; def.maxStack = 1;
        def.minRarity = def.maxRarity = Rarity::Epic; def.color = rgb(230, 170, 40);
    }
    {
        ItemDef& def = d[(size_t)ItemType::AmmoPickup];
        def.type = ItemType::AmmoPickup; def.name = "Ammo"; def.cls = ItemClass::Ammo; def.maxStack = 999; def.color = rgb(200, 200, 120);
    }
    {
        ItemDef& def = d[(size_t)ItemType::MaterialPickup];
        def.type = ItemType::MaterialPickup; def.name = "Materials"; def.cls = ItemClass::Mats; def.maxStack = 999; def.color = rgb(170, 130, 90);
    }
    return d;
}

const std::array<ItemDef, (size_t)ItemType::Count>& defs() {
    static const auto d = buildDefs();
    return d;
}

} // namespace

const ItemDef& itemDef(ItemType t) {
    size_t i = (size_t)t;
    if (i >= (size_t)ItemType::Count) i = 0;
    return defs()[i];
}

WeaponStats weaponStats(ItemType t, Rarity r) {
    WeaponStats w = itemDef(t).weapon;
    // Each rarity step adds ~5% damage and trims ~5% reload time.
    int step = (int)r;
    w.damage *= 1.0f + 0.05f * step;
    w.reloadTime *= 1.0f - 0.05f * step;
    if (t == ItemType::PumpShotgun) w.damage = 8.0f + 0.5f * step; // pellets
    return w;
}

Color4 rarityColor(Rarity r) {
    switch (r) {
        case Rarity::Common: return rgb(170, 170, 170);
        case Rarity::Uncommon: return rgb(96, 170, 58);
        case Rarity::Rare: return rgb(73, 172, 242);
        case Rarity::Epic: return rgb(177, 91, 226);
        case Rarity::Legendary: return rgb(211, 120, 53);
        default: return rgb(255, 255, 255);
    }
}

int ammoPickupAmount(AmmoType a) {
    switch (a) {
        case AmmoType::Light: return 36;
        case AmmoType::Medium: return 20;
        case AmmoType::Heavy: return 6;
        case AmmoType::Shells: return 5;
        case AmmoType::Rockets: return 3;
        default: return 0;
    }
}

int ammoMax(AmmoType a) {
    switch (a) {
        case AmmoType::Rockets: return 60;
        default: return 999;
    }
}

ItemStack makeItem(ItemType t, Rarity r, int count) {
    ItemStack s;
    s.type = t;
    s.rarity = r;
    s.count = (uint16_t)count;
    const ItemDef& d = itemDef(t);
    if (d.cls == ItemClass::Gun) s.clip = (uint16_t)weaponStats(t, r).magSize;
    return s;
}

ItemStack makeAmmo(AmmoType t, int amount) {
    ItemStack s;
    s.type = ItemType::AmmoPickup;
    s.count = (uint16_t)amount;
    s.extra = (uint8_t)t;
    return s;
}

ItemStack makeMats(Material m, int amount) {
    ItemStack s;
    s.type = ItemType::MaterialPickup;
    s.count = (uint16_t)amount;
    s.extra = (uint8_t)m;
    return s;
}

ItemStack makeAmmoFor(ItemType weapon) {
    AmmoType a = itemDef(weapon).ammo;
    if (a == AmmoType::None) return {};
    return makeAmmo(a, ammoPickupAmount(a));
}

static Rarity rollRarity(Rng& rng, LootSource src, Rarity lo, Rarity hi) {
    // Weighted rarity table; chests and supply drops skew higher.
    float w[5] = {40, 30, 18, 9, 3};
    if (src == LootSource::Chest) { w[0] = 25; w[1] = 32; w[2] = 25; w[3] = 13; w[4] = 5; }
    if (src == LootSource::SupplyDrop) { w[0] = 0; w[1] = 0; w[2] = 20; w[3] = 50; w[4] = 30; }
    float total = 0;
    for (int i = (int)lo; i <= (int)hi; i++) total += w[i];
    if (total <= 0) return hi;
    float r = rng.uniform() * total;
    for (int i = (int)lo; i <= (int)hi; i++) {
        r -= w[i];
        if (r <= 0) return (Rarity)i;
    }
    return hi;
}

ItemStack rollWeapon(Rng& rng, LootSource src) {
    struct Entry { ItemType t; float w; };
    static const Entry table[] = {
        {ItemType::AssaultRifle, 20}, {ItemType::BurstRifle, 8}, {ItemType::ScopedRifle, 4},
        {ItemType::PumpShotgun, 14},  {ItemType::TacticalShotgun, 10}, {ItemType::SMG, 14},
        {ItemType::Pistol, 10},       {ItemType::SniperRifle, 4},  {ItemType::RocketLauncher, 3},
        {ItemType::Minigun, 2},       {ItemType::Grenade, 6},      {ItemType::ImpulseGrenade, 4},
        {ItemType::LaunchPad, 1.5f},
    };
    float total = 0;
    for (auto& e : table) total += e.w;
    float r = rng.uniform() * total;
    ItemType pick = ItemType::AssaultRifle;
    for (auto& e : table) {
        r -= e.w;
        if (r <= 0) { pick = e.t; break; }
    }
    const ItemDef& d = itemDef(pick);
    Rarity rar = rollRarity(rng, src, d.minRarity, d.maxRarity);
    int count = 1;
    if (d.cls == ItemClass::Throwable) count = pick == ItemType::Grenade ? 3 : 2;
    return makeItem(pick, rar, count);
}

ItemStack rollConsumable(Rng& rng) {
    struct Entry { ItemType t; float w; int count; };
    static const Entry table[] = {
        {ItemType::Bandages, 30, 5}, {ItemType::Medkit, 14, 1}, {ItemType::SmallShield, 26, 3},
        {ItemType::ShieldPotion, 18, 1}, {ItemType::RegenSoda, 8, 1}, {ItemType::MegaFlask, 3, 1},
    };
    float total = 0;
    for (auto& e : table) total += e.w;
    float r = rng.uniform() * total;
    for (auto& e : table) {
        r -= e.w;
        if (r <= 0) return makeItem(e.t, itemDef(e.t).minRarity, e.count);
    }
    return makeItem(ItemType::Bandages, Rarity::Common, 5);
}

std::string itemDisplayName(const ItemStack& s) {
    if (s.type == ItemType::AmmoPickup) return AMMO_NAMES[s.extra % (int)AmmoType::Count];
    if (s.type == ItemType::MaterialPickup) return MATERIAL_NAMES[s.extra % 4];
    const ItemDef& d = itemDef(s.type);
    if (d.cls == ItemClass::Gun) return std::string(RARITY_NAMES[(int)s.rarity]) + " " + d.name;
    return d.name;
}

} // namespace si

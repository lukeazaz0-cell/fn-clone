// Item / weapon definitions and loot tables.
#pragma once
#include <string>

#include "../common/math.h"
#include "defs.h"

namespace si {

struct WeaponStats {
    float damage = 0;          // per pellet/bullet
    float headshotMul = 2.0f;
    float fireInterval = 0.1f; // seconds between shots (or bursts)
    int pellets = 1;
    int burst = 1;             // bullets per trigger pull
    float burstInterval = 0.06f;
    int magSize = 30;
    float reloadTime = 2.0f;
    float range = 200.0f;
    float falloffStart = 50.0f;
    float falloffMin = 0.6f;   // damage multiplier at max range
    float spreadHip = 0.035f;  // radians
    float spreadAds = 0.01f;
    float bloomPerShot = 0.01f;
    float maxBloom = 0.06f;
    float structureMul = 1.0f; // damage multiplier against builds/props
    bool automatic = true;
    bool projectile = false;   // rockets / grenades
    float projectileSpeed = 0;
    float explosionRadius = 0;
    float adsZoom = 1.3f;
    float equipTime = 0.4f;
    float spinUp = 0.0f;       // minigun
};

struct ConsumableStats {
    float useTime = 1.0f;
    float heal = 0;            // health restored
    float healCap = 100;       // cannot heal above this
    float shield = 0;
    float shieldCap = 100;
    float overTimeHeal = 0;    // per second (regen soda)
    float overTimeShield = 0;
    float overTimeDuration = 0;
    int maxStack = 15;
    bool movementWhileUsing = true;
};

struct ItemDef {
    ItemType type = ItemType::None;
    const char* name = "";
    ItemClass cls = ItemClass::None;
    AmmoType ammo = AmmoType::None;
    int maxStack = 1;
    Rarity minRarity = Rarity::Common;
    Rarity maxRarity = Rarity::Legendary;
    // Base weapon stats; rarity scales damage/reload through weaponStats().
    WeaponStats weapon;
    ConsumableStats consumable;
    Color4 color;             // tint used for the held model
};

const ItemDef& itemDef(ItemType t);
WeaponStats weaponStats(ItemType t, Rarity r);
Color4 rarityColor(Rarity r);
int ammoPickupAmount(AmmoType a);
int ammoMax(AmmoType a);

// A concrete item stack (inventory slot or floor pickup).
struct ItemStack {
    ItemType type = ItemType::None;
    Rarity rarity = Rarity::Common;
    uint16_t count = 0;   // stack size, or ammo amount for AmmoPickup, mats for MaterialPickup
    uint16_t clip = 0;    // loaded ammo for guns
    uint8_t extra = 0;    // AmmoType for AmmoPickup, Material for MaterialPickup
    bool empty() const { return type == ItemType::None || count == 0; }
};

// Loot rolling
enum class LootSource : uint8_t { Floor, Chest, SupplyDrop, AmmoBox };
ItemStack rollWeapon(Rng& rng, LootSource src);
ItemStack rollConsumable(Rng& rng);
ItemStack makeAmmoFor(ItemType weapon);
ItemStack makeAmmo(AmmoType t, int amount);
ItemStack makeMats(Material m, int amount);
ItemStack makeItem(ItemType t, Rarity r, int count = 1);

std::string itemDisplayName(const ItemStack& s);

} // namespace si

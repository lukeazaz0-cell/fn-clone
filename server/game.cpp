#include "game.h"

#include <algorithm>
#include <chrono>
#include <set>

namespace si {

namespace {

struct StormPhaseDef { float wait, shrink, radius, damage; };
const StormPhaseDef kStorm[] = {
    {75, 60, 470, 1}, {60, 45, 280, 1}, {50, 40, 160, 2}, {40, 35, 90, 5},
    {30, 30, 45, 8},  {25, 25, 20, 10}, {20, 25, 8, 10},  {15, 30, 0, 10},
};
constexpr int kStormPhases = sizeof(kStorm) / sizeof(kStorm[0]);
constexpr float BUS_SPEED = 42.0f;

bool newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

} // namespace

std::vector<uint8_t> packItemSpawn(const WorldItem& it) {
    ByteWriter w;
    w.u8(EV_ITEM_SPAWN);
    w.u32(it.id);
    w.u8((uint8_t)it.stack.type);
    w.u8((uint8_t)it.stack.rarity);
    w.u16(it.stack.count);
    w.u16(it.stack.clip);
    w.u8(it.stack.extra);
    w.vec3(it.pos);
    return w.buf;
}

static std::vector<uint8_t> packStructAdd(const Structure& s) {
    ByteWriter w;
    w.u8(EV_STRUCT_ADD);
    w.u32(s.id);
    w.u8((uint8_t)s.piece);
    w.i16((int16_t)s.gx); w.i16((int16_t)s.gy); w.i16((int16_t)s.gz);
    w.u8(s.rot);
    w.u8((uint8_t)s.mat);
    w.u8(s.edit);
    w.f32(s.hp);
    w.f32(s.maxHp);
    w.f32(s.buildTime);
    w.f32(s.age);
    w.u16(s.owner);
    w.u8(s.team);
    return w.buf;
}

// =============================================================================== setup

void Game::init(const GameConfig& c) {
    cfg = c;
    map.generate(cfg.mapSeed);
    world.setMap(&map);
    chestOpened.assign(map.chests.size(), 0);
    ammoBoxOpened.assign(map.ammoBoxes.size(), 0);
    rng_.reseed((uint64_t)std::chrono::steady_clock::now().time_since_epoch().count(), 3);
    setPhase(MatchPhase::Warmup);
    phaseTimer = cfg.warmupSeconds;
}

void Game::setPhase(MatchPhase p) {
    phase = p;
    if (log) log(std::string("phase -> ") + PHASE_NAMES[(int)p]);
}

int Game::humanCount() const {
    int n = 0;
    for (auto& [id, p] : players) if (!p.bot) n++;
    return n;
}
int Game::botCount() const {
    int n = 0;
    for (auto& [id, p] : players) if (p.bot) n++;
    return n;
}
int Game::aliveCount() const {
    int n = 0;
    for (auto& [id, p] : players) if (p.active()) n++;
    return n;
}
int Game::aliveTeams() const {
    std::set<uint8_t> t;
    for (auto& [id, p] : players) if (p.active()) t.insert(p.team);
    return (int)t.size();
}

std::vector<uint8_t> Game::evPlayerInfo(const Player& p) const {
    ByteWriter w;
    w.u8(EV_PLAYER_INFO);
    w.u16(p.id);
    w.str(p.name);
    w.u8(p.team);
    w.u8(p.bot ? 1 : 0);
    writeLoadout(w, p.loadout);
    return w.buf;
}

Player* Game::addPlayer(const std::string& name, const std::string& accountId, const Loadout& l, bool bot) {
    if ((int)players.size() >= cfg.maxPlayers) return nullptr;
    uint16_t id = nextPlayerId_++;
    if (nextPlayerId_ == 0xFFFF) nextPlayerId_ = 1;
    Player& p = players[id];
    p.id = id;
    p.name = name;
    p.accountId = accountId;
    p.bot = bot;
    p.loadout = l;
    // Team assignment: fill teams up to teamSize
    std::map<uint8_t, int> counts;
    for (auto& [pid, o] : players) if (pid != id) counts[o.team]++;
    uint8_t team = 0;
    for (auto& [t, n] : counts)
        if (n < cfg.teamSize && t != 0) { team = t; break; }
    if (team == 0) {
        for (uint8_t t = 1; t < 255; t++)
            if (!counts.count(t)) { team = t; break; }
    }
    p.team = team;
    if (bot) p.brain.actionId = 0;

    if (phase == MatchPhase::Warmup || phase == MatchPhase::Countdown) {
        spawnWarmup(p);
    } else if (phase == MatchPhase::Bus && busActive) {
        p.alive = true;
        p.move.mode = MoveMode::OnBus;
        p.move.pos = lerp3(busStart, busEnd, busProgress);
    } else if (phase == MatchPhase::Playing && bot) {
        // Late bots drop in from the sky inside the safe zone
        p.alive = true;
        float a = rng_.range(0, 2 * kPi), d = rng_.range(0, std::max(10.0f, storm.curRadius * 0.7f));
        float x = clampf(storm.curCenter.x + std::cos(a) * d, 50, WORLD_SIZE - 50);
        float z = clampf(storm.curCenter.y + std::sin(a) * d, 50, WORLD_SIZE - 50);
        p.move.pos = {x, world.terrainHeight(x, z) + 140, z};
        p.move.mode = MoveMode::Skydive;
    } else {
        // Humans joining a running match spectate.
        p.alive = false;
        p.eliminated = true;
        p.move.mode = MoveMode::Spectate;
    }
    if (emitEvent) emitEvent(evPlayerInfo(p), -1);
    if (log) log("player joined: " + name + (bot ? " [bot]" : ""));
    return &p;
}

void Game::removePlayer(uint16_t id) {
    auto it = players.find(id);
    if (it == players.end()) return;
    Player& p = it->second;
    if (p.active() && (phase == MatchPhase::Playing || phase == MatchPhase::Bus)) eliminate(p, 0xFFFF, ItemType::None, 0);
    ByteWriter w;
    w.u8(EV_PLAYER_LEFT);
    w.u16(id);
    players.erase(it);
    emit(w);
    checkEnd();
}

static const char* kBotNames[] = {"Ranger", "Pixel", "Nimbus", "Blaze", "Quill", "Tundra", "Echo", "Maverick", "Juniper",
                                  "Orbit", "Rook", "Sable", "Comet", "Drift", "Flint", "Harbor", "Indigo", "Kestrel",
                                  "Lumen", "Moss", "Nova", "Onyx", "Pebble", "Quartz", "Rune", "Sprocket", "Talon",
                                  "Umber", "Vesper", "Wren", "Yarrow", "Zephyr", "Cobalt", "Dune", "Ember", "Frost"};

void Game::addBots(int n) {
    const auto& cat = cosmeticCatalog();
    std::vector<const CosmeticDef*> outfits, bbs, picks, gliders;
    for (auto& c : cat) {
        if (c.type == CosmeticType::Outfit) outfits.push_back(&c);
        if (c.type == CosmeticType::BackBling) bbs.push_back(&c);
        if (c.type == CosmeticType::Pickaxe) picks.push_back(&c);
        if (c.type == CosmeticType::Glider) gliders.push_back(&c);
    }
    for (int i = 0; i < n; i++) {
        if ((int)players.size() >= cfg.maxPlayers) break;
        Loadout l;
        l.outfit = outfits[rng_.next() % outfits.size()]->id;
        l.backbling = bbs[rng_.next() % bbs.size()]->id;
        l.pickaxe = picks[rng_.next() % picks.size()]->id;
        l.glider = gliders[rng_.next() % gliders.size()]->id;
        std::string name = std::string(kBotNames[rng_.next() % (sizeof(kBotNames) / sizeof(kBotNames[0]))]) + std::to_string(rng_.irange(10, 99));
        addPlayer(name, "", l, true);
    }
}

void Game::removeBots(int n) {
    std::vector<uint16_t> ids;
    for (auto& [id, p] : players) if (p.bot) ids.push_back(id);
    // Prefer removing dead bots first
    std::stable_partition(ids.begin(), ids.end(), [&](uint16_t id) { return !players[id].active(); });
    for (int i = 0; i < n && i < (int)ids.size(); i++) removePlayer(ids[i]);
}

void Game::startNow() {
    if (phase == MatchPhase::Warmup) {
        setPhase(MatchPhase::Countdown);
        phaseTimer = std::min(5.0f, cfg.countdownSeconds);
    }
}

void Game::endMatchNow() {
    if (phase == MatchPhase::Playing || phase == MatchPhase::Bus) {
        winnerTeam = 0xFFFF;
        finishMatch();
    } else if (phase == MatchPhase::Ended) {
        phaseTimer = 0;
    }
}

void Game::broadcastMessage(const std::string& text, uint8_t kind) {
    ByteWriter w;
    w.u8(EV_MESSAGE);
    w.str(text);
    w.u8(kind);
    emit(w);
}

void Game::queueInput(Player& p, const InputCmd& c) {
    if (c.seq <= p.lastInputSeq) return;
    for (auto& q : p.inputQueue) if (q.seq == c.seq) return;
    // keep ordered
    auto it = p.inputQueue.begin();
    while (it != p.inputQueue.end() && it->seq < c.seq) ++it;
    p.inputQueue.insert(it, c);
    while (p.inputQueue.size() > 30) p.inputQueue.pop_front();
}

// =============================================================================== world reset / loot

void Game::spawnWarmup(Player& p) {
    const POI* poi = nullptr;
    std::vector<const POI*> majors;
    for (auto& x : map.pois) if (x.major) majors.push_back(&x);
    poi = majors[rng_.next() % majors.size()];
    float a = rng_.range(0, 2 * kPi), d = rng_.range(0, poi->radius);
    float x = poi->center.x + std::cos(a) * d, z = poi->center.y + std::sin(a) * d;
    if (!map.isLand(x, z)) { x = poi->center.x; z = poi->center.y; }
    p.move = MoveState();
    p.move.pos = {x, world.groundHeight({x, 200, z}, PLAYER_RADIUS, 300) + 0.05f, z};
    p.move.mode = MoveMode::Ground;
    p.alive = true;
    p.eliminated = false;
    p.dbno = false;
    p.health = MAX_HEALTH;
    p.shield = 50;
    p.action = ACT_NONE;
    giveLoadoutWarmup(p);
}

void Game::giveLoadoutWarmup(Player& p) {
    for (auto& s : p.inv) s = {};
    p.inv[0] = makeItem(ItemType::AssaultRifle, Rarity::Rare);
    p.inv[1] = makeItem(ItemType::PumpShotgun, Rarity::Rare);
    p.inv[2] = makeItem(ItemType::SMG, Rarity::Uncommon);
    for (int i = 0; i < (int)AmmoType::Count; i++) p.ammo[i] = 0;
    p.ammo[(int)AmmoType::Medium] = 300;
    p.ammo[(int)AmmoType::Shells] = 60;
    p.ammo[(int)AmmoType::Light] = 300;
    p.mats[0] = p.mats[1] = p.mats[2] = MAX_MATS;
    p.selected = 1;
}

void Game::resetWorldForMatch() {
    structures.clear(world);
    world.setMap(&map); // restores destroyed static props
    items.clear();
    projectiles.clear();
    launchPads.clear();
    supplyDrops.clear();
    chestOpened.assign(map.chests.size(), 0);
    ammoBoxOpened.assign(map.ammoBoxes.size(), 0);
    ByteWriter w;
    w.u8(EV_RESET_WORLD);
    emit(w);
    spawnInitialLoot();
    for (auto& [id, p] : players) {
        for (auto& s : p.inv) s = {};
        for (int i = 0; i < (int)AmmoType::Count; i++) p.ammo[i] = 0;
        p.mats[0] = p.mats[1] = p.mats[2] = 0;
        p.health = MAX_HEALTH;
        p.shield = 0;
        p.alive = true;
        p.eliminated = false;
        p.dbno = false;
        p.kills = 0;
        p.damageDealt = 0;
        p.placement = 0;
        p.selected = 0;
        p.buildMode = false;
        p.action = ACT_NONE;
        p.regenLeft = 0;
        p.emote = 0;
        p.lastAttacker = 0xFFFF;
        p.spectating = 0xFFFF;
        p.move = MoveState();
        p.move.mode = MoveMode::OnBus;
        p.inputQueue.clear();
    }
}

void Game::spawnInitialLoot() {
    for (auto& pos : map.floorLoot) {
        float r = rng_.uniform();
        if (r > 0.75f) continue;
        Vec3 p = pos + Vec3{0, 0.05f, 0};
        if (r < 0.45f) {
            ItemStack wpn = rollWeapon(rng_, LootSource::Floor);
            spawnItem(wpn, p, false);
            ItemStack am = makeAmmoFor(wpn.type);
            if (!am.empty()) spawnItem(am, p + Vec3{0.8f, 0, 0.3f}, false);
        } else if (r < 0.65f) {
            spawnItem(rollConsumable(rng_), p, false);
        } else {
            AmmoType t = (AmmoType)rng_.irange(1, 4);
            spawnItem(makeAmmo(t, ammoPickupAmount(t)), p, false);
        }
    }
}

uint32_t Game::spawnItem(const ItemStack& s, const Vec3& pos, bool scatter) {
    if (s.empty()) return INVALID_ID;
    WorldItem it;
    it.id = nextItemId_++;
    it.stack = s;
    Vec3 p = pos;
    if (scatter) {
        float a = rng_.range(0, 2 * kPi), d = rng_.range(0.5f, 1.8f);
        p.x += std::cos(a) * d;
        p.z += std::sin(a) * d;
    }
    p.y = world.groundHeight({p.x, pos.y + 1.0f, p.z}, 0.2f, pos.y + 1.0f);
    if (p.y < WATER_LEVEL - 0.5f) p.y = WATER_LEVEL - 0.5f;
    it.pos = p;
    items[it.id] = it;
    if (emitEvent) emitEvent(packItemSpawn(it), -1);
    return it.id;
}

void Game::removeItem(uint32_t id) {
    if (!items.erase(id)) return;
    ByteWriter w;
    w.u8(EV_ITEM_REMOVE);
    w.u32(id);
    emit(w);
}

int Game::addToInventory(Player& p, const ItemStack& s) {
    const ItemDef& d = itemDef(s.type);
    int left = s.count;
    if (d.maxStack > 1) {
        for (auto& slot : p.inv) {
            if (slot.type == s.type && slot.count < d.maxStack) {
                int add = std::min(left, d.maxStack - (int)slot.count);
                slot.count += add;
                left -= add;
                if (left == 0) return 0;
            }
        }
    }
    for (auto& slot : p.inv) {
        if (slot.empty()) {
            slot = s;
            slot.count = (uint16_t)std::min(left, d.maxStack);
            left -= slot.count;
            if (left == 0) return 0;
        }
    }
    return left;
}

bool Game::pickup(Player& p, uint32_t itemId) {
    auto it = items.find(itemId);
    if (it == items.end()) return false;
    ItemStack s = it->second.stack;
    Vec3 pos = it->second.pos;
    if (s.type == ItemType::AmmoPickup) {
        int& a = p.ammo[s.extra % (int)AmmoType::Count];
        int cap = ammoMax((AmmoType)s.extra);
        int take = std::min((int)s.count, cap - a);
        if (take <= 0) return false;
        a += take;
        removeItem(itemId);
        if (take < s.count) { s.count -= take; spawnItem(s, pos, false); }
        return true;
    }
    if (s.type == ItemType::MaterialPickup) {
        int& m = p.mats[s.extra % 3];
        int take = std::min((int)s.count, MAX_MATS - m);
        if (take <= 0) return false;
        m += take;
        removeItem(itemId);
        if (take < s.count) { s.count -= take; spawnItem(s, pos, false); }
        return true;
    }
    int left = addToInventory(p, s);
    if (left == s.count) {
        // Inventory full: swap with the held item.
        ItemStack* h = p.held();
        if (!h || h->empty()) return false;
        ItemStack dropped = *h;
        *h = s;
        removeItem(itemId);
        cancelAction(p);
        p.equipTimer = weaponStats(h->type, h->rarity).equipTime;
        spawnItem(dropped, pos, false);
        return true;
    }
    removeItem(itemId);
    if (left > 0) { s.count = (uint16_t)left; spawnItem(s, pos, false); }
    // Auto-equip the first weapon picked up when holding the pickaxe
    if (p.selected == 0 && itemDef(s.type).cls == ItemClass::Gun) {
        for (int i = 0; i < INVENTORY_SLOTS; i++)
            if (p.inv[i].type == s.type) { selectSlot(p, i + 1); break; }
    }
    return true;
}

void Game::dropSlot(Player& p, int slot) {
    if (slot < 1 || slot > INVENTORY_SLOTS) return;
    ItemStack& s = p.inv[slot - 1];
    if (s.empty()) return;
    Vec3 fwd{std::sin(p.yaw), 0, std::cos(p.yaw)};
    spawnItem(s, p.move.pos + fwd * 1.5f + Vec3{0, 0.5f, 0}, true);
    s = {};
    if (p.selected == slot) cancelAction(p);
}

void Game::dropInventory(Player& p) {
    Vec3 base = p.move.pos + Vec3{0, 0.5f, 0};
    for (auto& s : p.inv) {
        if (!s.empty()) spawnItem(s, base, true);
        s = {};
    }
    for (int i = 1; i < (int)AmmoType::Count; i++) {
        if (p.ammo[i] > 0) spawnItem(makeAmmo((AmmoType)i, p.ammo[i]), base, true);
        p.ammo[i] = 0;
    }
    for (int m = 0; m < 3; m++) {
        if (p.mats[m] > 0) spawnItem(makeMats((Material)m, p.mats[m]), base, true);
        p.mats[m] = 0;
    }
}

void Game::openChest(Player& p, uint32_t idx) {
    if (idx >= chestOpened.size() || chestOpened[idx]) return;
    chestOpened[idx] = 1;
    ByteWriter w;
    w.u8(EV_CHEST_OPEN);
    w.u32(idx);
    emit(w);
    Vec3 pos = map.chests[idx].pos + Vec3{0, 0.6f, 0};
    ItemStack wpn = rollWeapon(rng_, LootSource::Chest);
    spawnItem(wpn, pos, true);
    spawnItem(makeAmmoFor(wpn.type), pos, true);
    if (rng_.chance(0.7f)) spawnItem(rollConsumable(rng_), pos, true);
    spawnItem(makeMats(Material::Wood, 30), pos, true);
    (void)p;
}

void Game::openAmmoBox(Player& p, uint32_t idx) {
    if (idx >= ammoBoxOpened.size() || ammoBoxOpened[idx]) return;
    ammoBoxOpened[idx] = 1;
    ByteWriter w;
    w.u8(EV_AMMOBOX_OPEN);
    w.u32(idx);
    emit(w);
    Vec3 pos = map.ammoBoxes[idx] + Vec3{0, 0.5f, 0};
    for (int i = 0; i < 2; i++) {
        AmmoType t = (AmmoType)rng_.irange(1, 5);
        spawnItem(makeAmmo(t, ammoPickupAmount(t)), pos, true);
    }
    if (rng_.chance(0.25f)) spawnItem(makeItem(ItemType::Grenade, Rarity::Uncommon, 2), pos, true);
    (void)p;
}

void Game::openSupplyDrop(Player& p, uint32_t idx) {
    if (idx >= supplyDrops.size() || supplyDrops[idx].opened) return;
    SupplyDrop& d = supplyDrops[idx];
    d.opened = true;
    ByteWriter w;
    w.u8(EV_SUPPLY_OPEN);
    w.u32(d.id);
    emit(w);
    Vec3 pos = d.pos + Vec3{0, 0.6f, 0};
    ItemStack wpn = rollWeapon(rng_, LootSource::SupplyDrop);
    spawnItem(wpn, pos, true);
    spawnItem(makeAmmoFor(wpn.type), pos, true);
    spawnItem(rollConsumable(rng_), pos, true);
    spawnItem(makeMats(Material::Brick, 50), pos, true);
    spawnItem(makeMats(Material::Metal, 50), pos, true);
    (void)p;
}

// =============================================================================== match flow

void Game::startBus() {
    resetWorldForMatch();
    float a = rng_.range(0, 2 * kPi);
    Vec3 dir{std::cos(a), 0, std::sin(a)};
    Vec3 c{WORLD_SIZE / 2 + rng_.range(-200, 200), BUS_HEIGHT, WORLD_SIZE / 2 + rng_.range(-200, 200)};
    busStart = c - dir * (WORLD_SIZE * 0.72f);
    busEnd = c + dir * (WORLD_SIZE * 0.72f);
    busStart.y = busEnd.y = BUS_HEIGHT;
    busProgress = 0;
    busActive = true;
    matchStartTime = time;
    std::set<uint8_t> teams;
    for (auto& [id, p] : players) {
        teams.insert(p.team);
        p.move.pos = busStart;
        if (p.bot) {
            p.brain = BotBrain();
            // Bots pick a drop spot along the bus route
            p.brain.dropTime = rng_.range(0.12f, 0.8f);
            Vec3 along = lerp3(busStart, busEnd, p.brain.dropTime);
            float off = rng_.range(-250, 250);
            Vec3 side{-dir.z, 0, dir.x};
            Vec3 t = along + side * off;
            t.x = clampf(t.x, 150, WORLD_SIZE - 150);
            t.z = clampf(t.z, 150, WORLD_SIZE - 150);
            // Snap to a nearby POI sometimes
            const POI* poi = map.nearestPoi(t.x, t.z, 220);
            if (poi && rng_.chance(0.7f)) t = {poi->center.x + rng_.range(-30, 30), 0, poi->center.y + rng_.range(-30, 30)};
            p.brain.dropTarget = t;
        }
    }
    teamsAtStart = (int)teams.size();
    nextPlacement = (int)players.size();
    storm = StormState();
    storm.phase = 0;
    storm.curCenter = {WORLD_SIZE / 2, WORLD_SIZE / 2};
    storm.curRadius = WORLD_SIZE * 0.85f;
    storm.damage = kStorm[0].damage;
    storm.timer = kStorm[0].wait;
    pickNextStormCircle();
    setPhase(MatchPhase::Bus);
    broadcastMessage("The drop ship is leaving! Press SPACE to jump.", 1);
}

void Game::pickNextStormCircle() {
    if (storm.phase >= kStormPhases) return;
    float newR = kStorm[storm.phase].radius;
    float maxOff = std::max(0.0f, storm.curRadius - newR);
    if (storm.phase == 0) maxOff = std::min(maxOff, 300.0f);
    Vec2 best = storm.curCenter;
    for (int i = 0; i < 30; i++) {
        float a = rng_.range(0, 2 * kPi), d = std::sqrt(rng_.uniform()) * maxOff;
        Vec2 c{storm.curCenter.x + std::cos(a) * d, storm.curCenter.y + std::sin(a) * d};
        c.x = clampf(c.x, 150, WORLD_SIZE - 150);
        c.y = clampf(c.y, 150, WORLD_SIZE - 150);
        if (map.isLand(c.x, c.y)) { best = c; break; }
    }
    storm.nextCenter = best;
    storm.nextRadius = newR;
}

void Game::updateStorm(float dt) {
    if (storm.finished || storm.paused) return;
    storm.timer -= dt * cfg.stormSpeed;
    if (!storm.shrinking) {
        if (storm.timer <= 0) {
            storm.shrinking = true;
            storm.startCenter = storm.curCenter;
            storm.startRadius = storm.curRadius;
            storm.timer = kStorm[storm.phase].shrink;
            broadcastMessage("The storm is closing in!", 2);
        }
    } else {
        float total = kStorm[storm.phase].shrink;
        float t = clampf(1.0f - storm.timer / total, 0, 1);
        storm.curCenter = storm.startCenter + (storm.nextCenter - storm.startCenter) * t;
        storm.curRadius = lerpf(storm.startRadius, storm.nextRadius, t);
        if (storm.timer <= 0) {
            storm.curCenter = storm.nextCenter;
            storm.curRadius = storm.nextRadius;
            storm.shrinking = false;
            storm.phase++;
            if (storm.phase >= kStormPhases) {
                storm.finished = true;
                storm.damage = 10;
            } else {
                storm.damage = kStorm[storm.phase].damage;
                storm.timer = kStorm[storm.phase].wait;
                pickNextStormCircle();
                // Supply drops on some phases
                if (storm.phase == 1 || storm.phase == 3 || storm.phase == 5) {
                    SupplyDrop d;
                    d.id = nextDropId_++;
                    float a = rng_.range(0, 2 * kPi), r = rng_.range(0, storm.nextRadius * 0.8f);
                    float x = storm.nextCenter.x + std::cos(a) * r, z = storm.nextCenter.y + std::sin(a) * r;
                    d.groundY = world.groundHeight({x, 300, z}, 1.0f, 300);
                    d.pos = {x, d.groundY + 160, z};
                    supplyDrops.push_back(d);
                    ByteWriter w;
                    w.u8(EV_SUPPLY_DROP);
                    w.u32(d.id);
                    w.vec3(d.pos);
                    w.f32(d.groundY);
                    emit(w);
                    broadcastMessage("A supply drop is incoming!", 1);
                }
            }
        }
    }
}

void Game::updateBus(float dt) {
    if (!busActive) return;
    float len = (busEnd - busStart).len();
    busProgress += dt * BUS_SPEED / len;
    Vec3 pos = lerp3(busStart, busEnd, std::min(1.0f, busProgress));
    bool anyOn = false;
    for (auto& [id, p] : players) {
        if (p.move.mode != MoveMode::OnBus) continue;
        p.move.pos = pos;
        bool eject = busProgress >= 1.0f;
        if (p.bot && busProgress >= p.brain.dropTime) eject = true;
        if (eject) {
            p.move.mode = MoveMode::Skydive;
            p.move.pos = pos + Vec3{0, -3, 0};
            p.move.vel = (busEnd - busStart).norm() * 8.0f;
        } else {
            anyOn = true;
        }
    }
    if (busProgress >= 1.0f || !anyOn) {
        if (busProgress >= 1.0f) busActive = false;
        if (phase == MatchPhase::Bus && !anyOn) setPhase(MatchPhase::Playing);
    }
}

void Game::checkEnd() {
    if (phase != MatchPhase::Playing && phase != MatchPhase::Bus) return;
    int teams = aliveTeams();
    if ((teamsAtStart >= 2 && teams <= 1) || teams == 0) {
        winnerTeam = 0xFFFF;
        for (auto& [id, p] : players)
            if (p.active()) winnerTeam = p.team;
        finishMatch();
    }
}

void Game::finishMatch() {
    setPhase(MatchPhase::Ended);
    phaseTimer = 12.0f;
    busActive = false;
    std::vector<MatchResultEntry> results;
    for (auto& [id, p] : players) {
        if (p.team == winnerTeam) p.placement = 1;
        else if (p.placement == 0) p.placement = std::max(2, aliveTeams() + 1);
        MatchResultEntry e;
        e.accountId = p.accountId;
        e.name = p.name;
        e.bot = p.bot;
        e.placement = p.placement;
        e.kills = p.kills;
        e.damage = p.damageDealt;
        e.survived = time - matchStartTime;
        results.push_back(e);
        ByteWriter w;
        w.u8(EV_MATCH_RESULT);
        w.u8((uint8_t)std::min(255, p.placement));
        w.u16((uint16_t)p.kills);
        w.u16(winnerTeam);
        w.f32(p.damageDealt);
        w.u16((uint16_t)players.size());
        emit(w, p.id);
    }
    std::string winners;
    for (auto& [id, p] : players)
        if (p.team == winnerTeam) winners += (winners.empty() ? "" : ", ") + p.name;
    if (!winners.empty()) broadcastMessage(winners + " won the match!", 1);
    if (onMatchEnd && !resultsReported) onMatchEnd(results);
    resultsReported = true;
}

// =============================================================================== tick

void Game::tick(float dt) {
    time += dt;

    // --- inputs / bots
    botAccum_ += dt;
    int botSteps = 0;
    while (botAccum_ >= SIM_DT) { botAccum_ -= SIM_DT; botSteps++; }
    for (auto& [id, p] : players) {
        if (p.bot) {
            for (int i = 0; i < botSteps; i++) {
                InputCmd in;
                botThink(p, SIM_DT, in);
                simulatePlayer(p, in, SIM_DT);
            }
        } else {
            int processed = 0;
            while (!p.inputQueue.empty() && processed < 6) {
                InputCmd in = p.inputQueue.front();
                p.inputQueue.pop_front();
                p.lastInputSeq = in.seq;
                simulatePlayer(p, in, SIM_DT);
                processed++;
            }
        }
    }

    // --- phase logic
    switch (phase) {
        case MatchPhase::Warmup: {
            for (auto& [id, p] : players) {
                if (!p.alive) {
                    p.respawnTimer -= dt;
                    if (p.respawnTimer <= 0) spawnWarmup(p);
                }
            }
            if (humanCount() >= cfg.minHumansToStart || (cfg.minHumansToStart <= 0 && !players.empty())) {
                phaseTimer -= dt;
                if (phaseTimer <= 0) {
                    setPhase(MatchPhase::Countdown);
                    phaseTimer = cfg.countdownSeconds;
                }
            } else {
                phaseTimer = cfg.warmupSeconds;
            }
            break;
        }
        case MatchPhase::Countdown: {
            for (auto& [id, p] : players)
                if (!p.alive) { p.respawnTimer -= dt; if (p.respawnTimer <= 0) spawnWarmup(p); }
            phaseTimer -= dt;
            if (phaseTimer <= 0) {
                int want = cfg.fillBotsToMax ? cfg.maxPlayers - (int)players.size() : cfg.defaultBots - botCount();
                if (want > 0) addBots(want);
                startBus();
            }
            break;
        }
        case MatchPhase::Bus:
        case MatchPhase::Playing: {
            updateBus(dt);
            updateStorm(dt);
            // Storm damage once per second
            for (auto& [id, p] : players) {
                if (!p.active() || p.move.mode == MoveMode::OnBus) continue;
                float d = dist2d(p.move.pos.xz(), storm.curCenter);
                if (d > storm.curRadius) {
                    p.stormAccum += dt;
                    if (p.stormAccum >= 1.0f) {
                        p.stormAccum -= 1.0f;
                        damagePlayer(p, storm.damage, 0xFFFF, ItemType::None, KF_STORM, true);
                    }
                } else {
                    p.stormAccum = 0;
                }
            }
            checkEnd();
            break;
        }
        case MatchPhase::Ended: {
            phaseTimer -= dt;
            if (phaseTimer <= 0) {
                if (cfg.resetAfterMatch) {
                    // Back to warmup with the same humans; bots are removed.
                    std::vector<uint16_t> bots;
                    for (auto& [id, p] : players) if (p.bot) bots.push_back(id);
                    for (auto b : bots) {
                        players.erase(b);
                        ByteWriter w;
                        w.u8(EV_PLAYER_LEFT);
                        w.u16(b);
                        emit(w);
                    }
                    structures.clear(world);
                    world.setMap(&map);
                    items.clear();
                    ByteWriter w;
                    w.u8(EV_RESET_WORLD);
                    emit(w);
                    resultsReported = false;
                    setPhase(MatchPhase::Warmup);
                    phaseTimer = cfg.warmupSeconds;
                    for (auto& [id, p] : players) { p.kills = 0; p.placement = 0; spawnWarmup(p); }
                } else {
                    wantsExit = true;
                }
            }
            break;
        }
    }

    updateProjectiles(dt);
    updateSupplyDrops(dt);
    updateStructures(dt);
}

void Game::updateStructures(float dt) {
    for (auto& [id, s] : structures.byId) {
        if (s.age < s.buildTime) {
            s.age += dt;
            s.hp = std::min(s.maxHp, s.hp + s.maxHp * 0.9f / s.buildTime * dt);
        }
    }
}

void Game::updateSupplyDrops(float dt) {
    for (auto& d : supplyDrops) {
        if (d.pos.y > d.groundY) d.pos.y = std::max(d.groundY, d.pos.y - 9.0f * dt);
    }
}

// Projectile stats: guns scale with rarity, throwables use their base definition.
static WeaponStats projectileStats(ItemType t, Rarity r) {
    const ItemDef& d = itemDef(t);
    return d.cls == ItemClass::Gun ? weaponStats(t, r) : d.weapon;
}

void Game::updateProjectiles(float dt) {
    for (auto& pr : projectiles) {
        if (!pr.alive) continue;
        WeaponStats ws = projectileStats(pr.type, pr.rarity);
        auto detonate = [&](const Vec3& at) {
            if (pr.type == ItemType::ImpulseGrenade) {
                effects.push_back({FX_EXPLOSION, pr.owner, at, {}, 1});
                for (auto& [id, p] : players) {
                    if (!p.active()) continue;
                    Vec3 off = p.move.pos + Vec3{0, 1, 0} - at;
                    if (off.len() > ws.explosionRadius) continue;
                    p.move.vel = off.norm() * 18.0f + Vec3{0, 16, 0};
                    p.move.mode = MoveMode::Air;
                    p.move.fallStartY = p.move.pos.y + 1000;
                }
            } else if (ws.explosionRadius > 0) {
                explode(at, ws.explosionRadius, ws.damage, ws.structureMul, pr.owner, pr.team, pr.type);
            }
            pr.alive = false;
        };
        pr.fuse -= dt;
        if (pr.stuck) {
            // Follow the player a sticky charge is attached to
            if (pr.stuckTo != 0xFFFF) {
                auto it = players.find(pr.stuckTo);
                if (it != players.end() && it->second.active()) pr.pos = it->second.move.pos + pr.stuckOffset;
            }
            if (pr.fuse <= 0) detonate(pr.pos);
            continue;
        }
        pr.vel.y -= GRAVITY * ws.projectileGravity * dt;
        Vec3 step = pr.vel * dt;
        float len = step.len();
        Vec3 dir = step / std::max(len, 1e-5f);
        RayHit h = world.raycast(pr.pos, dir, len);
        float tp;
        bool head = false;
        uint16_t victim = raycastPlayers(pr.pos, dir, len, pr.owner, tp, head);
        bool hitPlayer = victim != 0xFFFF && (!h.hit || tp < h.t);
        if (hitPlayer) {
            Vec3 at = pr.pos + dir * tp;
            auto vit = players.find(victim);
            if (ws.stick && ws.explosionRadius > 0 && vit != players.end()) {
                // Sticky charge attaches to the player
                pr.stuck = true;
                pr.stuckTo = victim;
                pr.stuckOffset = at - vit->second.move.pos;
                pr.pos = at;
            } else if (ws.explosionRadius > 0) {
                detonate(at);
            } else if (vit != players.end()) {
                // Direct-hit bolt
                damagePlayer(vit->second, std::round(ws.damage * (head ? ws.headshotMul : 1.0f)), pr.owner, pr.type, head ? KF_HEADSHOT : 0);
                effects.push_back({FX_IMPACT, pr.owner, at, {}, 0});
                pr.alive = false;
            }
            continue;
        }
        if (h.hit) {
            if (ws.explodeOnImpact) { detonate(h.point); continue; }
            if (ws.stick) {
                pr.pos = h.point + h.normal * 0.05f;
                pr.stuck = true;
                pr.vel = {};
                if (ws.explosionRadius <= 0) {
                    // Bolts damage what they hit and stay embedded briefly
                    if (!h.terrain) damageShape(h.shapeId, ws.damage * ws.structureMul, nullptr, false);
                    effects.push_back({FX_IMPACT, pr.owner, h.point, h.normal, 0});
                    pr.fuse = std::min(pr.fuse, 2.0f);
                }
                continue;
            }
            if (ws.bounce) {
                pr.pos = h.point + h.normal * 0.05f;
                pr.vel = (pr.vel - h.normal * (2 * pr.vel.dot(h.normal))) * 0.45f;
            } else {
                pr.alive = false;
            }
        } else {
            pr.pos += step;
        }
        if (pr.fuse <= 0) {
            if (ws.explosionRadius > 0 || pr.type == ItemType::ImpulseGrenade) detonate(pr.pos);
            else pr.alive = false;
        }
        if (pr.pos.y < -30) pr.alive = false;
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.alive; }), projectiles.end());
}

// =============================================================================== player sim

void Game::simulatePlayer(Player& p, const InputCmd& in, float dt) {
    p.time += dt;
    p.yaw = in.yaw;
    p.pitch = clampf(in.pitch, -1.5f, 1.5f);
    p.buttons = in.buttons;
    if (p.eliminated || !p.alive) { p.prevButtons = in.buttons; return; }

    MoveParams mp;
    mp.launchPads = &launchPads;
    mp.dbno = p.dbno;
    const ItemStack* h = p.held();
    if (p.action == ACT_CONSUME && h && !itemDef(h->type).consumable.movementWhileUsing) mp.rooted = true;
    if (p.action == ACT_REVIVE) mp.rooted = true;
    if (h && h->type == ItemType::Minigun) mp.speedMul = 0.85f;

    MoveInput mi;
    mi.yaw = in.yaw;
    mi.pitch = in.pitch;
    mi.fwd = in.fwd;
    mi.right = in.right;
    mi.buttons = in.buttons;
    if (p.emote && (in.fwd || in.right || (in.buttons & (IN_FIRE | IN_JUMP)))) p.emote = 0;
    if (p.emote) { mi.fwd = mi.right = 0; }
    MoveEvents ev = stepMovement(p.move, mi, world, map, dt, mp);
    if (ev.fallDamage > 0 && phase != MatchPhase::Warmup && phase != MatchPhase::Countdown)
        damagePlayer(p, ev.fallDamage, 0xFFFF, ItemType::None, KF_FALL, true);
    if (!p.alive) { p.prevButtons = in.buttons; return; }

    if (p.dbno) {
        p.dbnoBleedAccum += dt;
        if (p.dbnoBleedAccum >= 1.0f) {
            p.dbnoBleedAccum -= 1.0f;
            p.dbnoHealth -= 3;
            if (p.dbnoHealth <= 0) eliminate(p, p.lastAttacker, ItemType::None, 0);
        }
    }
    if (p.regenLeft > 0) {
        p.regenLeft -= dt;
        p.regenAccum += dt;
        while (p.regenAccum >= 1.0f) {
            p.regenAccum -= 1.0f;
            if (p.health < MAX_HEALTH) p.health = std::min(MAX_HEALTH, p.health + 1);
            else p.shield = std::min(MAX_SHIELD, p.shield + 1);
        }
    }

    bool canUseItems = p.move.mode == MoveMode::Ground || p.move.mode == MoveMode::Air;
    uint16_t pressed = in.buttons & ~p.prevButtons;
    if ((pressed & IN_INTERACT) && canUseItems) startInteract(p);
    if ((p.action == ACT_INTERACT || p.action == ACT_REVIVE) && !(in.buttons & IN_INTERACT)) cancelAction(p);
    if (canUseItems) updateWeapon(p, in, dt);
    else if (p.action == ACT_RELOAD || p.action == ACT_CONSUME) cancelAction(p);
    p.prevButtons = in.buttons;
}

void Game::selectSlot(Player& p, int slot) {
    slot = std::max(0, std::min(INVENTORY_SLOTS, slot));
    if (p.selected == slot && !p.buildMode) return;
    if (p.action == ACT_RELOAD || p.action == ACT_CONSUME) cancelAction(p);
    p.selected = slot;
    p.buildMode = false;
    p.burstLeft = 0;
    p.spin = 0;
    const ItemStack* h = p.held();
    p.equipTimer = h && !h->empty() ? (itemDef(h->type).cls == ItemClass::Gun ? weaponStats(h->type, h->rarity).equipTime : 0.25f) : 0.2f;
}

void Game::cancelAction(Player& p) {
    p.action = ACT_NONE;
    p.actionTime = 0;
    p.actionTotal = 0;
    p.actionTarget = INVALID_ID;
}

void Game::startReload(Player& p) {
    ItemStack* h = p.held();
    if (!h || itemDef(h->type).cls != ItemClass::Gun) return;
    WeaponStats ws = weaponStats(h->type, h->rarity);
    AmmoType at = itemDef(h->type).ammo;
    if (h->clip >= ws.magSize || p.ammo[(int)at] <= 0 || ws.reloadTime <= 0) return;
    if (p.action == ACT_RELOAD) return;
    p.action = ACT_RELOAD;
    p.actionTime = 0;
    p.actionTotal = ws.reloadTime;
}

void Game::finishAction(Player& p) {
    ActionKind a = p.action;
    uint32_t target = p.actionTarget;
    int kind = p.actionTargetKind;
    cancelAction(p);
    switch (a) {
        case ACT_RELOAD: {
            ItemStack* h = p.held();
            if (!h) return;
            WeaponStats ws = weaponStats(h->type, h->rarity);
            AmmoType at = itemDef(h->type).ammo;
            int need = ws.magSize - h->clip;
            int take = std::min(need, p.ammo[(int)at]);
            h->clip += take;
            p.ammo[(int)at] -= take;
            break;
        }
        case ACT_CONSUME: {
            ItemStack* h = p.held();
            if (!h || h->empty() || itemDef(h->type).cls != ItemClass::Consumable) return;
            const ConsumableStats& c = itemDef(h->type).consumable;
            if (c.heal > 0 && p.health < c.healCap) p.health = std::min(c.healCap, p.health + c.heal);
            if (c.shield > 0 && p.shield < c.shieldCap) p.shield = std::min(c.shieldCap, p.shield + c.shield);
            if (c.overTimeDuration > 0) { p.regenLeft = c.overTimeDuration; p.regenAccum = 0; }
            h->count--;
            if (h->count == 0) *h = {};
            break;
        }
        case ACT_INTERACT: {
            if (kind == 1) openChest(p, target);
            else if (kind == 2) openAmmoBox(p, target);
            else if (kind == 3) openSupplyDrop(p, target);
            break;
        }
        case ACT_REVIVE: {
            auto it = players.find((uint16_t)target);
            if (it != players.end() && it->second.dbno && it->second.alive) {
                it->second.dbno = false;
                it->second.health = 30;
                it->second.shield = 0;
                broadcastMessage(p.name + " revived " + it->second.name, 0);
            }
            break;
        }
        default: break;
    }
}

void Game::startInteract(Player& p) {
    if (p.action == ACT_INTERACT || p.action == ACT_REVIVE) return;
    Vec3 origin = p.move.pos + Vec3{0, 1.0f, 0};
    Vec3 look = dirFromAngles(p.yaw, p.pitch);
    float bestScore = 1e9f;
    int bestKind = -1;
    uint32_t best = INVALID_ID;
    auto consider = [&](const Vec3& pos, int kind, uint32_t id, float range) {
        Vec3 d = pos - origin;
        float dist = d.len();
        if (dist > range) return;
        float facing = dist > 0.01f ? d.norm().dot(look) : 1.0f;
        float score = dist - facing * 1.5f;
        if (score < bestScore) { bestScore = score; bestKind = kind; best = id; }
    };
    if (!p.dbno) {
        for (auto& [id, o] : players)
            if (o.id != p.id && o.team == p.team && o.dbno && o.alive) consider(o.move.pos + Vec3{0, 0.5f, 0}, 4, o.id, INTERACT_RANGE);
        for (size_t i = 0; i < map.chests.size(); i++)
            if (!chestOpened[i] && std::fabs(map.chests[i].pos.x - origin.x) < 4 && std::fabs(map.chests[i].pos.z - origin.z) < 4)
                consider(map.chests[i].pos + Vec3{0, 0.5f, 0}, 1, (uint32_t)i, INTERACT_RANGE);
        for (size_t i = 0; i < map.ammoBoxes.size(); i++)
            if (!ammoBoxOpened[i] && std::fabs(map.ammoBoxes[i].x - origin.x) < 4 && std::fabs(map.ammoBoxes[i].z - origin.z) < 4)
                consider(map.ammoBoxes[i] + Vec3{0, 0.4f, 0}, 2, (uint32_t)i, INTERACT_RANGE);
        for (size_t i = 0; i < supplyDrops.size(); i++)
            if (!supplyDrops[i].opened && supplyDrops[i].pos.y <= supplyDrops[i].groundY + 0.1f)
                consider(supplyDrops[i].pos + Vec3{0, 0.6f, 0}, 3, (uint32_t)i, INTERACT_RANGE + 0.5f);
        for (auto& [id, it] : items)
            if (std::fabs(it.pos.x - origin.x) < 4 && std::fabs(it.pos.z - origin.z) < 4) consider(it.pos + Vec3{0, 0.3f, 0}, 0, id, INTERACT_RANGE);
    }
    if (bestKind < 0) return;
    if (bestKind == 0) { pickup(p, best); return; }
    p.action = bestKind == 4 ? ACT_REVIVE : ACT_INTERACT;
    p.actionTarget = best;
    p.actionTargetKind = bestKind;
    p.actionTime = 0;
    p.actionTotal = bestKind == 4 ? 6.0f : bestKind == 3 ? 1.0f : 0.35f;
    if (p.buildMode) p.buildMode = false;
}

void Game::updateWeapon(Player& p, const InputCmd& in, float dt) {
    p.fireCooldown -= dt;
    p.equipTimer -= dt;
    p.buildCooldown -= dt;
    p.bloom = std::max(0.0f, p.bloom - dt * 0.12f);
    if (p.action != ACT_NONE) {
        p.actionTime += dt;
        if (p.action == ACT_REVIVE) {
            auto it = players.find((uint16_t)p.actionTarget);
            if (it == players.end() || !it->second.dbno || (it->second.move.pos - p.move.pos).len() > INTERACT_RANGE + 1) { cancelAction(p); }
        }
        if (p.action != ACT_NONE && p.actionTime >= p.actionTotal) finishAction(p);
    }
    if (p.dbno) return;

    bool fire = in.buttons & IN_FIRE;
    bool firePressed = fire && !(p.prevButtons & IN_FIRE);
    uint16_t pressed = in.buttons & ~p.prevButtons;

    if (p.buildMode) {
        if (fire && p.buildCooldown <= 0) tryBuild(p);
        return;
    }
    if (p.selected == 0) {
        if (fire && p.fireCooldown <= 0 && p.equipTimer <= 0) {
            swingPickaxe(p);
            p.fireCooldown = 0.55f;
        }
        return;
    }
    ItemStack* h = p.held();
    if (!h || h->empty()) return;
    const ItemDef& d = itemDef(h->type);
    switch (d.cls) {
        case ItemClass::Gun: {
            WeaponStats ws = weaponStats(h->type, h->rarity);
            if (h->type == ItemType::Minigun) {
                p.spin = fire ? std::min(ws.spinUp, p.spin + dt) : std::max(0.0f, p.spin - dt * 2);
            }
            if ((pressed & IN_RELOAD) && h->clip < ws.magSize) startReload(p);
            if (p.burstLeft > 0) {
                p.burstTimer -= dt;
                if (p.burstTimer <= 0 && h->clip > 0) {
                    fireGun(p);
                    p.burstLeft--;
                    p.burstTimer = ws.burstInterval;
                } else if (h->clip == 0) {
                    p.burstLeft = 0;
                }
            }
            bool trigger = ws.automatic ? fire : firePressed;
            if (h->type == ItemType::Minigun && p.spin < ws.spinUp) trigger = false;
            if (trigger && p.fireCooldown <= 0 && p.equipTimer <= 0) {
                if (h->clip == 0) {
                    startReload(p);
                } else {
                    if (p.action == ACT_RELOAD || p.action == ACT_CONSUME) cancelAction(p);
                    fireGun(p);
                    p.burstLeft = ws.burst - 1;
                    p.burstTimer = ws.burstInterval;
                    p.fireCooldown = ws.fireInterval;
                }
            }
            if (h->clip == 0 && p.action == ACT_NONE && p.ammo[(int)d.ammo] > 0 && p.fireCooldown <= 0) startReload(p);
            break;
        }
        case ItemClass::Throwable: {
            if (firePressed && p.fireCooldown <= 0 && p.equipTimer <= 0) {
                throwItem(p);
                p.fireCooldown = d.weapon.fireInterval;
            }
            break;
        }
        case ItemClass::Consumable: {
            if (firePressed && p.action == ACT_NONE) {
                const ConsumableStats& c = d.consumable;
                bool useful = (c.heal > 0 && p.health < c.healCap) || (c.shield > 0 && p.shield < c.shieldCap) || c.overTimeDuration > 0;
                if (useful) {
                    p.action = ACT_CONSUME;
                    p.actionTime = 0;
                    p.actionTotal = c.useTime;
                }
            }
            break;
        }
        case ItemClass::Utility: {
            if (firePressed && h->type == ItemType::LaunchPad && p.move.mode == MoveMode::Ground) {
                Vec3 fwd{std::sin(p.yaw), 0, std::cos(p.yaw)};
                Vec3 at = p.move.pos + fwd * 2.8f;
                at.y = world.groundHeight({at.x, p.move.pos.y + 1.0f, at.z}, 0.5f, p.move.pos.y + 1.0f);
                if (std::fabs(at.y - p.move.pos.y) < 1.5f) {
                    launchPads.push_back(at);
                    ByteWriter w;
                    w.u8(EV_LAUNCHPAD_ADD);
                    w.vec3(at);
                    emit(w);
                    h->count--;
                    if (h->count == 0) *h = {};
                }
            }
            break;
        }
        default: break;
    }
}

Game::AimResult Game::aim(const Player& p, float spread) {
    bool ads = p.buttons & IN_ADS;
    CameraRig cam = thirdPersonCamera(p.move.pos, p.yaw, p.pitch, ads, p.move.crouched);
    Vec3 dir = cam.dir;
    if (spread > 0) {
        Vec3 up = std::fabs(dir.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 u = dir.cross(up).norm();
        Vec3 v = dir.cross(u).norm();
        float r = spread * std::sqrt(rng_.uniform());
        float th = rng_.range(0, 2 * kPi);
        dir = (dir * std::cos(r) + (u * std::cos(th) + v * std::sin(th)) * std::sin(r)).norm();
    }
    Vec3 eye = p.eye();
    float t0 = std::max(0.0f, (eye - cam.pos).dot(dir));
    Vec3 start = cam.pos + dir * t0;
    RayHit h = world.raycast(start, dir, 800);
    AimResult a;
    a.origin = eye;
    a.point = h.hit ? h.point : start + dir * 800;
    a.dir = (a.point - eye).norm();
    return a;
}

uint16_t Game::raycastPlayers(const Vec3& o, const Vec3& d, float maxT, uint16_t ignore, float& tOut, bool& head) const {
    uint16_t best = 0xFFFF;
    float bestT = maxT;
    uint8_t ignoreTeam = 0;
    auto self = players.find(ignore);
    if (self != players.end()) ignoreTeam = self->second.team;
    for (auto& [id, p] : players) {
        if (id == ignore || !p.active() || p.move.mode == MoveMode::OnBus) continue;
        if (ignoreTeam != 0 && p.team == ignoreTeam && cfg.teamSize > 1) continue;
        float h = p.dbno ? 0.8f : (p.move.crouched ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT);
        AABB box({p.move.pos.x - PLAYER_RADIUS, p.move.pos.y, p.move.pos.z - PLAYER_RADIUS},
                 {p.move.pos.x + PLAYER_RADIUS, p.move.pos.y + h, p.move.pos.z + PLAYER_RADIUS});
        float t;
        if (rayAABB(o, d, box, bestT, t) && t < bestT) {
            bestT = t;
            best = id;
            Vec3 hp = o + d * t;
            head = !p.dbno && hp.y > p.move.pos.y + h - 0.38f;
        }
    }
    tOut = bestT;
    return best;
}

void Game::fireGun(Player& p) {
    ItemStack* h = p.held();
    if (!h || h->clip == 0) return;
    WeaponStats ws = weaponStats(h->type, h->rarity);
    if (h->type != ItemType::Minigun) h->clip--;
    else {
        // Minigun consumes reserve ammo directly
        if (p.ammo[(int)AmmoType::Light] <= 0) { h->clip = 0; return; }
        p.ammo[(int)AmmoType::Light]--;
    }
    bool ads = p.buttons & IN_ADS;
    float spread = ads ? ws.spreadAds : ws.spreadHip;
    bool moving = p.move.vel.lenXZ() > 1.0f;
    if (moving && !ads) spread += 0.02f;
    if (p.move.mode == MoveMode::Air) spread += 0.035f;
    if (p.move.crouched) spread *= 0.8f;
    spread += p.bloom;
    bool firstShot = p.time - p.lastShotTime > 0.6f && ads && !moving && ws.pellets == 1;
    if (firstShot) spread = 0;
    p.lastShotTime = p.time;
    p.bloom = std::min(ws.maxBloom, p.bloom + ws.bloomPerShot);
    p.emote = 0;

    if (ws.projectile) {
        AimResult a = aim(p, spread);
        Projectile pr;
        pr.id = nextProjId_++;
        pr.type = h->type;
        pr.rarity = h->rarity;
        pr.owner = p.id;
        pr.team = p.team;
        pr.pos = a.origin + a.dir * 0.8f;
        pr.vel = a.dir * ws.projectileSpeed;
        pr.fuse = ws.fuse;
        projectiles.push_back(pr);
        return;
    }

    // Hit-scan pellets. Structures take the combined damage of all pellets that hit them.
    std::unordered_map<uint32_t, float> shapeDamage;
    std::unordered_map<uint16_t, std::pair<float, bool>> playerDamage;
    for (int i = 0; i < ws.pellets; i++) {
        AimResult a = aim(p, ws.pellets > 1 ? std::max(spread, ws.spreadAds) : spread);
        float range = ws.range;
        RayHit wh = world.raycast(a.origin, a.dir, range);
        float tp;
        bool head = false;
        uint16_t victim = raycastPlayers(a.origin, a.dir, wh.hit ? wh.t : range, p.id, tp, head);
        Vec3 end;
        if (victim != 0xFFFF) {
            end = a.origin + a.dir * tp;
            float falloff = 1.0f;
            if (tp > ws.falloffStart) falloff = lerpf(1.0f, ws.falloffMin, clampf((tp - ws.falloffStart) / std::max(1.0f, ws.range - ws.falloffStart), 0, 1));
            float dmg = ws.damage * falloff * (head ? ws.headshotMul : 1.0f);
            auto& pd = playerDamage[victim];
            pd.first += dmg;
            pd.second = pd.second || head;
        } else if (wh.hit) {
            end = wh.point;
            if (!wh.terrain) shapeDamage[wh.shapeId] += ws.damage * ws.structureMul;
            else effects.push_back({FX_IMPACT, p.id, wh.point, wh.normal, 0});
        } else {
            end = a.origin + a.dir * range;
        }
        if (i < 4) effects.push_back({FX_SHOT, p.id, a.origin, end, (uint8_t)h->type});
    }
    for (auto& [sid, dmg] : shapeDamage) {
        const Shape* s = world.shape(sid);
        if (s) effects.push_back({FX_IMPACT, p.id, s->box.center(), {}, (uint8_t)s->mat});
        damageShape(sid, dmg, &p, false);
    }
    for (auto& [vid, pd] : playerDamage) {
        auto it = players.find(vid);
        if (it != players.end()) damagePlayer(it->second, std::round(pd.first), p.id, h->type, pd.second ? KF_HEADSHOT : 0);
    }
}

void Game::swingPickaxe(Player& p) {
    p.emote = 0;
    AimResult a = aim(p, 0);
    float reach = 2.6f;
    RayHit wh = world.raycast(a.origin, a.dir, reach);
    float tp;
    bool head;
    uint16_t victim = raycastPlayers(a.origin, a.dir, wh.hit ? wh.t : reach, p.id, tp, head);
    if (victim != 0xFFFF) {
        auto it = players.find(victim);
        if (it != players.end()) damagePlayer(it->second, 20, p.id, ItemType::None, KF_PICKAXE);
        return;
    }
    // Generous harvesting: if the precise ray misses, sweep a short horizontal ray at chest height.
    if (!wh.hit || wh.terrain) {
        Vec3 fwd{std::sin(p.yaw), 0, std::cos(p.yaw)};
        wh = world.raycast(p.move.pos + Vec3{0, 1.1f, 0}, fwd, reach, false);
    }
    if (wh.hit && !wh.terrain) {
        const Shape* s = world.shape(wh.shapeId);
        effects.push_back({FX_HARVEST, p.id, wh.point, wh.normal, s ? (uint8_t)s->mat : (uint8_t)0});
        damageShape(wh.shapeId, 50, &p, true);
    }
}

void Game::throwItem(Player& p) {
    ItemStack* h = p.held();
    if (!h || h->empty()) return;
    AimResult a = aim(p, 0);
    Projectile pr;
    pr.id = nextProjId_++;
    pr.type = h->type;
    pr.rarity = h->rarity;
    pr.owner = p.id;
    pr.team = p.team;
    pr.pos = a.origin + a.dir * 0.6f;
    const WeaponStats& ws = itemDef(h->type).weapon;
    pr.vel = a.dir * ws.projectileSpeed + Vec3{0, 5, 0} + p.move.vel * 0.5f;
    pr.fuse = ws.fuse;
    projectiles.push_back(pr);
    h->count--;
    if (h->count == 0) *h = {};
}

void Game::tryBuild(Player& p) {
    BuildTarget t = computeBuildTarget(p.move.pos, p.yaw, p.pitch, p.buildPiece, p.buildRot);
    if (!structures.canPlace(t, world)) return;
    int mi = (int)p.buildMat;
    bool freeBuild = phase == MatchPhase::Warmup || phase == MatchPhase::Countdown;
    if (!freeBuild && p.mats[mi] < BUILD_COST) {
        // Fall back to any material with enough stock
        int alt = -1;
        for (int m = 0; m < 3; m++) if (p.mats[m] >= BUILD_COST) { alt = m; break; }
        if (alt < 0) return;
        mi = alt;
        p.buildMat = (Material)alt;
    }
    if (!freeBuild) p.mats[mi] -= BUILD_COST;
    Structure s;
    s.id = nextStructId_++;
    s.piece = t.piece;
    s.gx = t.gx; s.gy = t.gy; s.gz = t.gz;
    s.rot = t.rot;
    s.mat = (Material)mi;
    s.maxHp = materialMaxHp(s.mat);
    s.hp = s.maxHp * 0.1f;
    s.buildTime = materialBuildTime(s.mat);
    s.age = 0;
    s.owner = p.id;
    s.team = p.team;
    Structure& added = structures.add(s, world);
    if (emitEvent) emitEvent(packStructAdd(added), -1);
    effects.push_back({FX_BUILD, p.id, pieceBounds(s.piece, s.gx, s.gy, s.gz, s.rot).center(), {}, (uint8_t)s.mat});
    p.buildCooldown = 0.09f;
}

void Game::tryEdit(Player& p) {
    AimResult a = aim(p, 0);
    RayHit h = world.raycast(a.origin, a.dir, 7.0f, false);
    if (!h.hit) return;
    const Shape* sh = world.shape(h.shapeId);
    if (!sh || sh->structure == INVALID_ID) return;
    Structure* s = structures.find(sh->structure);
    if (!s || s->team != p.team) return;
    uint8_t e = s->piece == PieceType::Ramp ? (uint8_t)((s->rot + 1) % 4) : nextEdit(s->piece, s->edit);
    // Ramp rotation must keep the slot key consistent: StructureSet::setEdit handles rot for ramps.
    structures.setEdit(s->id, e, world);
    ByteWriter w;
    w.u8(EV_STRUCT_EDIT);
    w.u32(s->id);
    w.u8(e);
    emit(w);
}

void Game::handleAction(Player& p, const ClientAction& a) {
    if (!newer(a.id, p.lastActionId) && !p.bot) return;
    p.lastActionId = a.id;
    switch (a.type) {
        case ActionType::SelectSlot: selectSlot(p, a.a); break;
        case ActionType::SetBuildMode:
            if (p.dbno || p.eliminated) break;
            if (a.a) {
                if (p.action == ACT_RELOAD || p.action == ACT_CONSUME) cancelAction(p);
                p.buildMode = true;
                p.buildPiece = (PieceType)std::min<int>(a.b, 3);
                p.emote = 0;
            } else {
                p.buildMode = false;
            }
            break;
        case ActionType::SetBuildMat: p.buildMat = (Material)std::min<int>(a.a, 2); break;
        case ActionType::RotatePiece: p.buildRot = (uint8_t)((p.buildRot + 1) % 4); break;
        case ActionType::EditPiece: if (p.active() && !p.dbno) tryEdit(p); break;
        case ActionType::DropSlot: if (p.active()) dropSlot(p, a.a); break;
        case ActionType::SwapSlots:
            if (a.a >= 1 && a.a <= INVENTORY_SLOTS && a.b >= 1 && a.b <= INVENTORY_SLOTS) {
                std::swap(p.inv[a.a - 1], p.inv[a.b - 1]);
                cancelAction(p);
            }
            break;
        case ActionType::Emote:
            if (p.active() && !p.dbno && p.move.mode == MoveMode::Ground) { p.emote = (uint8_t)(a.a + 1); p.emoteTime = 0; p.buildMode = false; }
            break;
        case ActionType::JumpFromBus:
            if (p.move.mode == MoveMode::OnBus && busProgress > 0.03f) {
                p.move.mode = MoveMode::Skydive;
                p.move.pos = p.move.pos + Vec3{0, -3, 0};
                p.move.vel = (busEnd - busStart).norm() * 8.0f;
            }
            break;
        case ActionType::ToggleGlide:
            if (p.move.mode == MoveMode::Air && p.move.canGlide) p.move.mode = MoveMode::Glide;
            break;
        case ActionType::DropMats: {
            int m = std::min<int>(a.a, 2);
            int amount = std::min(p.mats[m], std::max(10, a.b * 10));
            if (amount > 0 && p.active()) {
                p.mats[m] -= amount;
                spawnItem(makeMats((Material)m, amount), p.move.pos + Vec3{std::sin(p.yaw) * 1.5f, 0.5f, std::cos(p.yaw) * 1.5f}, false);
            }
            break;
        }
        case ActionType::Spectate: {
            if (!p.eliminated) break;
            std::vector<uint16_t> alive;
            for (auto& [id, o] : players) if (o.active()) alive.push_back(id);
            if (alive.empty()) break;
            auto it = std::upper_bound(alive.begin(), alive.end(), p.spectating);
            p.spectating = it == alive.end() ? alive.front() : *it;
            break;
        }
        default: break;
    }
}

// =============================================================================== damage

void Game::damagePlayer(Player& victim, float amount, uint16_t attacker, ItemType weapon, uint8_t killFlags, bool ignoreShield) {
    if (!victim.active() || amount <= 0) return;
    if (victim.move.mode == MoveMode::OnBus) return;
    Player* att = nullptr;
    if (attacker != 0xFFFF) {
        auto it = players.find(attacker);
        if (it != players.end()) att = &it->second;
    }
    if (att && att->id != victim.id && att->team == victim.team && cfg.teamSize > 1) return; // no friendly fire
    if (att && att->id != victim.id) {
        victim.lastAttacker = attacker;
        victim.lastAttackTime = time;
    }
    if (victim.bot) victim.brain.lastDamageTaken = time;
    uint8_t dflags = (killFlags & KF_HEADSHOT) ? DF_HEADSHOT : 0;

    if (victim.dbno) {
        victim.dbnoHealth -= amount;
        if (att) att->damageDealt += amount;
        if (victim.dbnoHealth <= 0) eliminate(victim, attacker, weapon, killFlags);
    } else {
        float rest = amount;
        if (!ignoreShield && victim.shield > 0) {
            float s = std::min(victim.shield, rest);
            victim.shield -= s;
            rest -= s;
            dflags |= DF_SHIELD;
        }
        victim.health -= rest;
        if (att && att->id != victim.id) att->damageDealt += amount;
        if (victim.health <= 0) {
            victim.health = 0;
            dflags |= DF_KILL;
            bool teammateUp = false;
            for (auto& [id, o] : players)
                if (o.id != victim.id && o.team == victim.team && o.active() && !o.dbno) teammateUp = true;
            bool canKnock = cfg.teamSize > 1 && teammateUp && phase != MatchPhase::Warmup && phase != MatchPhase::Countdown &&
                            !(killFlags & KF_STORM);
            if (canKnock) knock(victim, attacker, weapon, killFlags);
            else eliminate(victim, attacker, weapon, killFlags);
        }
    }
    if (att && att->id != victim.id && !att->bot) {
        ByteWriter w;
        w.u8(EV_DAMAGE);
        w.vec3(victim.move.pos + Vec3{0, 2.0f, 0});
        w.u16((uint16_t)std::round(amount));
        w.u8(dflags);
        emit(w, att->id);
    }
    if (!victim.bot) {
        ByteWriter w;
        w.u8(EV_TAKE_DAMAGE);
        w.vec3(att ? att->move.pos : victim.move.pos);
        w.u16((uint16_t)std::round(amount));
        w.u8(att ? 1 : 0);
        emit(w, victim.id);
    }
}

void Game::knock(Player& victim, uint16_t attacker, ItemType weapon, uint8_t flags) {
    victim.dbno = true;
    victim.dbnoHealth = 100;
    victim.health = 1;
    victim.buildMode = false;
    victim.emote = 0;
    cancelAction(victim);
    ByteWriter w;
    w.u8(EV_KILLFEED);
    w.u16(attacker);
    w.u16(victim.id);
    w.u8((uint8_t)weapon);
    w.u8(flags | KF_KNOCKED);
    emit(w);
}

void Game::eliminate(Player& victim, uint16_t killer, ItemType weapon, uint8_t flags) {
    if (!victim.active()) return;
    if (killer == 0xFFFF && victim.lastAttacker != 0xFFFF && time - victim.lastAttackTime < 15.0f) killer = victim.lastAttacker;
    ByteWriter w;
    w.u8(EV_KILLFEED);
    w.u16(killer);
    w.u16(victim.id);
    w.u8((uint8_t)weapon);
    w.u8(flags);
    emit(w);
    auto kit = players.find(killer);
    if (kit != players.end() && kit->second.id != victim.id && kit->second.team != victim.team) kit->second.kills++;

    victim.alive = false;
    victim.dbno = false;
    victim.buildMode = false;
    cancelAction(victim);
    if (phase == MatchPhase::Warmup || phase == MatchPhase::Countdown) {
        victim.respawnTimer = 3.0f;
        victim.move.mode = MoveMode::Dead;
        return;
    }
    victim.eliminated = true;
    victim.move.mode = MoveMode::Spectate;
    dropInventory(victim);
    victim.spectating = killer != 0xFFFF && kit != players.end() && kit->second.active() ? killer : 0xFFFF;
    // Team wipe: eliminate knocked teammates, assign placement.
    bool teamAlive = false;
    for (auto& [id, o] : players)
        if (o.team == victim.team && o.active() && !o.dbno) teamAlive = true;
    if (!teamAlive) {
        for (auto& [id, o] : players)
            if (o.team == victim.team && o.active() && o.dbno) {
                o.alive = false; o.eliminated = true; o.dbno = false; o.move.mode = MoveMode::Spectate; dropInventory(o);
            }
        int place = aliveTeams() + 1;
        for (auto& [id, o] : players)
            if (o.team == victim.team) o.placement = place;
    } else {
        for (auto& [id, o] : players)
            if (o.team == victim.team && o.active()) { victim.spectating = id; break; }
    }
    if (!victim.bot) {
        ByteWriter r;
        r.u8(EV_MATCH_RESULT);
        r.u8((uint8_t)std::min(255, std::max(victim.placement, 1)));
        r.u16((uint16_t)victim.kills);
        r.u16(0xFFFF);
        r.f32(victim.damageDealt);
        r.u16((uint16_t)players.size());
        if (!teamAlive) emit(r, victim.id);
    }
    checkEnd();
}

void Game::damageShape(uint32_t shapeId, float amount, Player* attacker, bool harvest) {
    Shape* s = world.shape(shapeId);
    if (!s || !s->alive) return;
    if (s->structure != INVALID_ID) {
        Structure* st = structures.find(s->structure);
        if (!st) return;
        st->hp -= amount;
        if (st->hp <= 0) {
            destroyStructure(st->id);
        } else {
            ByteWriter w;
            w.u8(EV_STRUCT_HP);
            w.u32(st->id);
            w.f32(st->hp);
            emit(w);
        }
        return;
    }
    if (!s->destructible) return;
    s->hp -= amount;
    if (harvest && attacker && s->mat != Material::None) {
        int gain = s->mat == Material::Wood ? rng_.irange(5, 8) : s->mat == Material::Brick ? rng_.irange(4, 7) : rng_.irange(3, 6);
        int& m = attacker->mats[(int)s->mat];
        m = std::min(MAX_MATS, m + gain);
    }
    if (s->hp <= 0) {
        world.removeShape(shapeId);
        ByteWriter w;
        w.u8(EV_SHAPE_REMOVE);
        w.u32(shapeId);
        emit(w);
    }
}

void Game::destroyStructure(uint32_t structId) {
    Structure* st = structures.find(structId);
    if (!st) return;
    AABB bounds = pieceBounds(st->piece, st->gx, st->gy, st->gz, st->rot);
    effects.push_back({FX_EXPLOSION, 0, bounds.center(), {}, 0});
    structures.remove(structId, world);
    ByteWriter w;
    w.u8(EV_STRUCT_REMOVE);
    w.u32(structId);
    emit(w);
    // Collapse anything that lost its connection to the ground.
    std::vector<uint32_t> fall = structures.findUnsupported(bounds, world);
    for (uint32_t id : fall) {
        structures.remove(id, world);
        ByteWriter w2;
        w2.u8(EV_STRUCT_REMOVE);
        w2.u32(id);
        emit(w2);
    }
}

void Game::explode(const Vec3& pos, float radius, float damage, float structMul, uint16_t owner, uint8_t team, ItemType weapon) {
    effects.push_back({FX_EXPLOSION, owner, pos, {}, 0});
    // Structures first so explosions punch through builds.
    std::set<uint32_t> structs;
    std::vector<uint32_t> statics;
    world.query(AABB(pos - Vec3{radius, radius, radius}, pos + Vec3{radius, radius, radius}), [&](const Shape& s) {
        if (s.structure != INVALID_ID) structs.insert(s.structure);
        else if (s.destructible) statics.push_back(s.id);
    });
    Player* att = nullptr;
    auto ait = players.find(owner);
    if (ait != players.end()) att = &ait->second;
    for (uint32_t sid : statics) damageShape(sid, damage * structMul * 0.6f, nullptr, false);
    for (uint32_t id : structs) {
        Structure* st = structures.find(id);
        if (!st) continue;
        st->hp -= damage * structMul;
        if (st->hp <= 0) destroyStructure(id);
        else {
            ByteWriter w;
            w.u8(EV_STRUCT_HP);
            w.u32(id);
            w.f32(st->hp);
            emit(w);
        }
    }
    (void)att;
    for (auto& [id, p] : players) {
        if (!p.active()) continue;
        if (cfg.teamSize > 1 && p.team == team && p.id != owner) continue;
        Vec3 c = p.move.pos + Vec3{0, 1.0f, 0};
        float d = (c - pos).len();
        if (d > radius) continue;
        if (!world.lineOfSight(pos + (c - pos).norm() * 0.3f, c)) continue;
        float f = 1.0f - 0.5f * (d / radius);
        damagePlayer(p, std::round(damage * f), owner, weapon, KF_EXPLOSION);
    }
}

// =============================================================================== join state

void Game::writeFullStateEvents(std::vector<std::vector<uint8_t>>& out) const {
    for (auto& [id, p] : players) out.push_back(evPlayerInfo(p));
    for (auto& s : world.staticShapes()) {
        if (!s.alive) {
            ByteWriter w;
            w.u8(EV_SHAPE_REMOVE);
            w.u32(s.id);
            out.push_back(w.buf);
        }
    }
    for (auto& [id, s] : structures.byId) out.push_back(packStructAdd(s));
    for (auto& [id, it] : items) out.push_back(packItemSpawn(it));
    for (size_t i = 0; i < chestOpened.size(); i++)
        if (chestOpened[i]) { ByteWriter w; w.u8(EV_CHEST_OPEN); w.u32((uint32_t)i); out.push_back(w.buf); }
    for (size_t i = 0; i < ammoBoxOpened.size(); i++)
        if (ammoBoxOpened[i]) { ByteWriter w; w.u8(EV_AMMOBOX_OPEN); w.u32((uint32_t)i); out.push_back(w.buf); }
    for (auto& lp : launchPads) { ByteWriter w; w.u8(EV_LAUNCHPAD_ADD); w.vec3(lp); out.push_back(w.buf); }
    for (auto& d : supplyDrops) {
        ByteWriter w; w.u8(EV_SUPPLY_DROP); w.u32(d.id); w.vec3(d.pos); w.f32(d.groundY); out.push_back(w.buf);
        if (d.opened) { ByteWriter o; o.u8(EV_SUPPLY_OPEN); o.u32(d.id); out.push_back(o.buf); }
    }
}

} // namespace si

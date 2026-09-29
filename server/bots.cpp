// Server-side bot AI. Bots produce the same InputCmd a human client would send, so
// they move with the shared movement code and fire through the normal weapon path.
#include <algorithm>

#include "game.h"

namespace si {

namespace {

struct DiffParams { float aimNoise, turnRate, reaction, sight, buildChance, headshotBias; };
DiffParams diffParams(BotDifficulty d) {
    switch (d) {
        case BotDifficulty::Easy: return {0.10f, 3.0f, 0.80f, 55.0f, 0.0f, 0.0f};
        case BotDifficulty::Medium: return {0.055f, 5.0f, 0.45f, 80.0f, 0.35f, 0.15f};
        case BotDifficulty::Hard: return {0.03f, 8.0f, 0.28f, 110.0f, 0.7f, 0.35f};
        case BotDifficulty::Insane: return {0.012f, 14.0f, 0.12f, 150.0f, 0.95f, 0.6f};
    }
    return {0.05f, 5.0f, 0.4f, 80.0f, 0.3f, 0.1f};
}

float angleTo(const Vec3& from, const Vec3& to, float& pitch) {
    Vec3 d = to - from;
    float h = std::sqrt(d.x * d.x + d.z * d.z);
    pitch = std::atan2(d.y, std::max(0.01f, h));
    return std::atan2(d.x, d.z);
}

float approachAngle(float cur, float target, float maxStep) {
    float diff = wrapAngle(target - cur);
    if (std::fabs(diff) <= maxStep) return target;
    return wrapAngle(cur + (diff > 0 ? maxStep : -maxStep));
}

int weaponScore(const ItemStack& s) {
    if (s.empty()) return -1;
    const ItemDef& d = itemDef(s.type);
    if (d.cls != ItemClass::Gun) return -1;
    return (int)s.rarity * 10 + 5;
}

} // namespace

void Game::botAct(Player& p, ActionType t, uint8_t a, uint8_t b) {
    ClientAction ca;
    ca.id = ++p.brain.actionId;
    ca.type = t;
    ca.a = a;
    ca.b = b;
    handleAction(p, ca);
}

bool Game::botNeedsHeal(Player& p) {
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        const ItemStack& s = p.inv[i];
        if (s.empty() || itemDef(s.type).cls != ItemClass::Consumable) continue;
        const ConsumableStats& c = itemDef(s.type).consumable;
        if ((c.heal > 0 && p.health < std::min(c.healCap, 70.0f)) || (c.shield > 0 && p.shield < c.shieldCap - 20)) return true;
    }
    return false;
}

bool Game::botFindEnemy(Player& p) {
    DiffParams dp = diffParams(cfg.botDifficulty);
    float best = dp.sight;
    uint16_t found = 0xFFFF;
    Vec3 eye = p.eye();
    for (auto& [id, o] : players) {
        if (id == p.id || !o.active() || o.team == p.team || o.move.mode == MoveMode::OnBus) continue;
        float d = (o.move.pos - p.move.pos).len();
        // Bots notice whoever shot them even from further away
        float sight = (o.id == p.lastAttacker && time - p.lastAttackTime < 3.0f) ? dp.sight * 1.6f : best;
        if (d > sight) continue;
        // Field of view: 200 degrees unless very close or recently shot at
        Vec3 dir = (o.move.pos - p.move.pos).norm();
        Vec3 fwd{std::sin(p.yaw), 0, std::cos(p.yaw)};
        if (d > 12 && dir.dot(fwd) < -0.2f && o.id != p.lastAttacker) continue;
        if (!world.lineOfSight(eye, o.move.pos + Vec3{0, 1.3f, 0})) continue;
        best = d;
        found = id;
    }
    if (found != p.brain.enemy && found != 0xFFFF) p.brain.reactionTimer = dp.reaction * rng_.range(0.7f, 1.3f);
    p.brain.enemy = found;
    return found != 0xFFFF;
}

void Game::botChooseWeapon(Player& p, float dist) {
    int bestSlot = 0, bestScore = -1000;
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        const ItemStack& s = p.inv[i];
        if (s.empty() || itemDef(s.type).cls != ItemClass::Gun) continue;
        AmmoType at = itemDef(s.type).ammo;
        if (s.clip == 0 && p.ammo[(int)at] == 0) continue;
        int score = (int)s.rarity * 3;
        switch (s.type) {
            case ItemType::PumpShotgun:
            case ItemType::TacticalShotgun: score += dist < 12 ? 40 : dist < 20 ? 10 : -30; break;
            case ItemType::SMG: score += dist < 25 ? 30 : 0; break;
            case ItemType::AssaultRifle:
            case ItemType::BurstRifle: score += dist < 12 ? 10 : 25; break;
            case ItemType::ScopedRifle:
            case ItemType::SniperRifle: score += dist > 60 ? 45 : -10; break;
            case ItemType::RocketLauncher: score += dist > 15 && dist < 80 ? 20 : -40; break;
            case ItemType::Minigun: score += dist < 50 ? 28 : 5; break;
            case ItemType::Pistol: score += 5; break;
            default: break;
        }
        if (score > bestScore) { bestScore = score; bestSlot = i + 1; }
    }
    if (p.selected != bestSlot && !(p.action == ACT_RELOAD)) botAct(p, ActionType::SelectSlot, (uint8_t)bestSlot);
}

void Game::botMoveTo(Player& p, const Vec3& target, InputCmd& out, bool sprint) {
    float pitch;
    float yaw = angleTo(p.move.pos, target, pitch);
    out.yaw = approachAngle(p.yaw, yaw, 6.0f * SIM_DT);
    out.pitch = 0;
    out.fwd = 1;
    if (sprint) out.buttons |= IN_SPRINT;
    // Jump over small obstacles / when stuck
    BotBrain& b = p.brain;
    b.stuckTimer += SIM_DT;
    if (b.stuckTimer > 1.2f) {
        float moved = distXZ(p.move.pos, b.lastPos);
        if (moved < 1.0f) {
            b.jumpTimer = 0.2f;
            if (rng_.chance(0.5f)) b.harvestTimer = 1.4f; // smash through
            else b.wanderTimer = 1.5f;
        }
        b.lastPos = p.move.pos;
        b.stuckTimer = 0;
    }
    if (b.jumpTimer > 0) {
        b.jumpTimer -= SIM_DT;
        if (p.move.onGround && (int)(b.jumpTimer * 60) % 2 == 0) out.buttons |= IN_JUMP;
    }
    if (b.wanderTimer > 0) {
        b.wanderTimer -= SIM_DT;
        out.right = 1;
    }
    if (target.y > p.move.pos.y + 3.0f && p.move.onGround && distXZ(target, p.move.pos) < 6.0f && b.buildCooldown <= 0 &&
        p.mats[0] + p.mats[1] + p.mats[2] >= BUILD_COST) {
        // Ramp up to reach elevated loot
        bool wasBuild = p.buildMode;
        PieceType piece = p.buildPiece;
        p.buildMode = true;
        p.buildPiece = PieceType::Ramp;
        p.yaw = yaw;
        tryBuild(p);
        p.buildMode = wasBuild;
        p.buildPiece = piece;
        b.buildCooldown = 0.6f;
    }
}

void Game::botLoot(Player& p, InputCmd& out) {
    BotBrain& b = p.brain;
    int guns = 0, freeSlots = 0, worst = 1000;
    for (auto& s : p.inv) {
        if (s.empty()) freeSlots++;
        else if (itemDef(s.type).cls == ItemClass::Gun) { guns++; worst = std::min(worst, weaponScore(s)); }
    }
    // (Re)select a target
    if (b.thinkTimer <= 0 || !b.hasTarget) {
        b.hasTarget = false;
        float bestD = 70.0f;
        for (auto& [id, it] : items) {
            float d = distXZ(it.pos, p.move.pos);
            if (d >= bestD || std::fabs(it.pos.y - p.move.pos.y) > 12) continue;
            const ItemDef& def = itemDef(it.stack.type);
            bool want = false;
            if (def.cls == ItemClass::Gun) want = freeSlots > 0 || weaponScore(it.stack) > worst + 5;
            else if (def.cls == ItemClass::Consumable || def.cls == ItemClass::Throwable) want = freeSlots > 0 && guns >= 1;
            else if (def.cls == ItemClass::Ammo) want = p.ammo[it.stack.extra % (int)AmmoType::Count] < 120;
            else if (def.cls == ItemClass::Mats) want = p.mats[it.stack.extra % 3] < 500;
            if (!want) continue;
            bestD = d * (def.cls == ItemClass::Gun && guns < 2 ? 0.5f : 1.0f);
            b.hasTarget = true;
            b.lootKind = 0;
            b.lootTarget = id;
            b.target = it.pos;
        }
        for (size_t i = 0; i < map.chests.size(); i++) {
            if (chestOpened[i]) continue;
            float d = distXZ(map.chests[i].pos, p.move.pos) * 0.7f;
            if (d >= bestD || std::fabs(map.chests[i].pos.y - p.move.pos.y) > 12) continue;
            bestD = d;
            b.hasTarget = true;
            b.lootKind = 1;
            b.lootTarget = (uint32_t)i;
            b.target = map.chests[i].pos;
        }
        for (size_t i = 0; i < supplyDrops.size(); i++) {
            if (supplyDrops[i].opened || supplyDrops[i].pos.y > supplyDrops[i].groundY + 0.1f) continue;
            float d = distXZ(supplyDrops[i].pos, p.move.pos) * 0.5f;
            if (d >= bestD) continue;
            bestD = d;
            b.hasTarget = true;
            b.lootKind = 3;
            b.lootTarget = (uint32_t)i;
            b.target = supplyDrops[i].pos;
        }
    }
    if (!b.hasTarget) {
        // Wander towards the safe zone, or a random POI during warmup/early game
        Vec3 goal;
        if (phase == MatchPhase::Playing || phase == MatchPhase::Bus) goal = {storm.nextCenter.x, 0, storm.nextCenter.y};
        else goal = map.warmupSpawnCenter;
        if (distXZ(goal, p.move.pos) < 25) {
            if (b.wanderTimer <= 0) b.wanderTimer = 0;
            float a = rng_.range(0, 2 * kPi);
            goal = p.move.pos + Vec3{std::cos(a) * 30, 0, std::sin(a) * 30};
        }
        goal.y = world.terrainHeight(goal.x, goal.z);
        botMoveTo(p, goal, out, true);
        return;
    }
    float d = distXZ(b.target, p.move.pos);
    if (d < 2.2f && std::fabs(b.target.y - p.move.pos.y) < 2.5f) {
        if (b.lootKind == 0) pickup(p, b.lootTarget);
        else if (b.lootKind == 1) openChest(p, b.lootTarget);
        else if (b.lootKind == 3) openSupplyDrop(p, b.lootTarget);
        b.hasTarget = false;
        b.thinkTimer = 0;
        return;
    }
    // Target vanished?
    if (b.lootKind == 0 && !items.count(b.lootTarget)) { b.hasTarget = false; return; }
    if (b.lootKind == 1 && chestOpened[b.lootTarget]) { b.hasTarget = false; return; }
    botMoveTo(p, b.target, out, d > 10);
}

void Game::botFight(Player& p, float dt, InputCmd& out) {
    BotBrain& b = p.brain;
    DiffParams dp = diffParams(cfg.botDifficulty);
    auto it = players.find(b.enemy);
    if (it == players.end() || !it->second.active()) { b.enemy = 0xFFFF; return; }
    Player& e = it->second;
    float dist = (e.move.pos - p.move.pos).len();
    if (p.selected == 0 || p.buildMode || b.thinkTimer <= 0) botChooseWeapon(p, dist);

    // Aim with lead and noise
    const ItemStack* h = p.held();
    Vec3 aimPt = e.move.pos + Vec3{0, e.dbno ? 0.4f : (rng_.chance(dp.headshotBias) ? 1.65f : 1.15f), 0};
    if (h && !h->empty()) {
        WeaponStats ws = weaponStats(h->type, h->rarity);
        if (ws.projectile && ws.projectileSpeed > 0) aimPt += e.move.vel * (dist / ws.projectileSpeed);
    }
    float pitch;
    float yaw = angleTo(p.eye(), aimPt, pitch);
    yaw += rng_.range(-dp.aimNoise, dp.aimNoise);
    pitch += rng_.range(-dp.aimNoise, dp.aimNoise) * 0.6f;
    b.aimYaw = approachAngle(b.aimYaw, yaw, dp.turnRate * dt);
    b.aimPitch = approachAngle(b.aimPitch, pitch, dp.turnRate * dt);
    out.yaw = b.aimYaw;
    out.pitch = b.aimPitch;

    // Movement: strafe + keep preferred distance
    b.strafeTimer -= dt;
    if (b.strafeTimer <= 0) {
        b.strafeTimer = rng_.range(0.5f, 1.4f);
        b.strafeDir = rng_.chance(0.5f) ? 1 : -1;
    }
    out.right = (int8_t)b.strafeDir;
    float preferred = 18.0f;
    if (h && (h->type == ItemType::PumpShotgun || h->type == ItemType::TacticalShotgun)) preferred = 5.0f;
    if (h && (h->type == ItemType::SniperRifle || h->type == ItemType::ScopedRifle)) preferred = 60.0f;
    out.fwd = dist > preferred + 5 ? 1 : dist < preferred - 4 ? -1 : 0;
    if (cfg.botDifficulty >= BotDifficulty::Hard && p.move.onGround && rng_.chance(0.02f)) out.buttons |= IN_JUMP;

    // Defensive building when taking fire
    b.buildCooldown -= dt;
    if (time - b.lastDamageTaken < 0.8f && b.buildCooldown <= 0 && p.move.onGround && rng_.chance(dp.buildChance) &&
        p.mats[0] + p.mats[1] + p.mats[2] >= BUILD_COST * 2) {
        float savedYaw = p.yaw;
        p.yaw = angleTo(p.move.pos, e.move.pos, pitch);
        p.pitch = 0;
        p.buildMode = true;
        p.buildPiece = PieceType::Wall;
        tryBuild(p);
        if (cfg.botDifficulty >= BotDifficulty::Hard && rng_.chance(0.5f)) {
            p.buildPiece = PieceType::Ramp;
            tryBuild(p);
        }
        p.buildMode = false;
        p.yaw = savedYaw;
        b.buildCooldown = rng_.range(1.5f, 3.0f);
    }

    // Fire
    if (b.reactionTimer > 0) { b.reactionTimer -= dt; return; }
    float aimErr = std::fabs(wrapAngle(yaw - b.aimYaw)) + std::fabs(pitch - b.aimPitch);
    if (!h || h->empty()) {
        // Pickaxe rush
        out.fwd = 1;
        if (dist < 2.5f) out.buttons |= IN_FIRE;
        return;
    }
    WeaponStats ws = weaponStats(h->type, h->rarity);
    if (dist > ws.range * 1.1f) { out.fwd = 1; return; }
    if (aimErr < 0.15f) {
        if (ws.automatic) out.buttons |= IN_FIRE;
        else {
            b.fireHold += dt;
            if (b.fireHold > 0.05f) { out.buttons |= IN_FIRE; b.fireHold = -0.05f; }
        }
        if (ws.adsZoom > 2.0f || dist > 30) out.buttons |= IN_ADS;
    }
}

void Game::botThink(Player& p, float dt, InputCmd& out) {
    BotBrain& b = p.brain;
    out.seq = 0;
    out.yaw = p.yaw;
    out.pitch = p.pitch;
    out.fwd = 0;
    out.right = 0;
    out.buttons = 0;
    if (!p.active()) return;

    switch (p.move.mode) {
        case MoveMode::OnBus: return;
        case MoveMode::Skydive:
        case MoveMode::Glide: {
            Vec3 t = b.dropTarget;
            float d = distXZ(t, p.move.pos);
            float pitch;
            out.yaw = angleTo(p.move.pos, {t.x, p.move.pos.y, t.z}, pitch);
            if (d > 25) out.fwd = 1;
            if (p.move.mode == MoveMode::Skydive && d < 120) out.buttons |= IN_DIVE;
            return;
        }
        default: break;
    }
    if (p.move.mode == MoveMode::Air && p.move.canGlide && rng_.chance(0.05f)) out.buttons |= IN_JUMP;

    b.thinkTimer -= dt;
    bool rethink = b.thinkTimer <= 0;
    if (rethink) {
        botFindEnemy(p);
        if (p.buildMode) p.buildMode = false;
    }

    // Harvest when stuck
    if (b.harvestTimer > 0) {
        b.harvestTimer -= dt;
        if (p.selected != 0) botAct(p, ActionType::SelectSlot, 0);
        out.buttons |= IN_FIRE;
        out.fwd = 1;
        if (rethink) b.thinkTimer = 0.3f;
        return;
    }

    // Storm avoidance
    bool inGame = phase == MatchPhase::Playing || phase == MatchPhase::Bus;
    if (inGame) {
        float dCur = dist2d(p.move.pos.xz(), storm.curCenter);
        float dNext = dist2d(p.move.pos.xz(), storm.nextCenter);
        bool outside = dCur > storm.curRadius - 5;
        bool rotate = dNext > storm.nextRadius * 0.9f && (storm.shrinking || storm.timer < 25.0f);
        if (outside || (rotate && b.enemy == 0xFFFF)) {
            Vec2 c = outside ? storm.curCenter : storm.nextCenter;
            Vec3 goal{c.x, 0, c.y};
            goal.y = world.terrainHeight(goal.x, goal.z);
            // Heal while running if the storm is hurting us
            botMoveTo(p, goal, out, true);
            if (b.enemy != 0xFFFF && !outside) botFight(p, dt, out);
            if (rethink) b.thinkTimer = 0.3f;
            return;
        }
    }

    if (b.enemy != 0xFFFF) {
        botFight(p, dt, out);
    } else if (botNeedsHeal(p) && time - b.lastDamageTaken > 2.5f) {
        // Heal up
        int slot = -1;
        for (int i = 0; i < INVENTORY_SLOTS; i++) {
            const ItemStack& s = p.inv[i];
            if (s.empty() || itemDef(s.type).cls != ItemClass::Consumable) continue;
            const ConsumableStats& c = itemDef(s.type).consumable;
            if ((c.shield > 0 && p.shield < c.shieldCap - 20) || (c.heal > 0 && p.health < c.healCap - 10)) { slot = i + 1; break; }
        }
        if (slot > 0) {
            if (p.selected != slot) botAct(p, ActionType::SelectSlot, (uint8_t)slot);
            if (p.action != ACT_CONSUME) {
                b.fireHold += dt;
                if (b.fireHold > 0.05f) { out.buttons |= IN_FIRE; b.fireHold = -0.05f; }
            }
        }
    } else {
        // Reload idle weapons
        const ItemStack* h = p.held();
        if (h && !h->empty() && itemDef(h->type).cls == ItemClass::Gun && h->clip < weaponStats(h->type, h->rarity).magSize / 2 &&
            rng_.chance(0.02f))
            out.buttons |= IN_RELOAD;
        if (p.selected == 0 || (h && itemDef(h->type).cls != ItemClass::Gun)) botChooseWeapon(p, 20);
        botLoot(p, out);
    }
    if (rethink) b.thinkTimer = rng_.range(0.25f, 0.45f);
}

} // namespace si

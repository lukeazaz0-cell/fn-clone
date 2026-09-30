// In-match client: world replica, prediction, interpolation, input, rendering, HUD.
#pragma once
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio.h"
#include "net_client.h"
#include "raylib.h"
#include "render.h"
#include "shared/game/building.h"
#include "shared/game/movement.h"

namespace client {

struct Settings {
    float sensitivity = 0.0025f;
    float fov = 80.0f;
    float volume = 0.7f;
    float viewDistance = 650.0f;
    bool showFps = true;
    bool invertY = false;
    bool shadows = true;
    bool autotest = false;          // scripted run for screenshots/CI (STORM_AUTOTEST)
    bool autotestVehicles = false;  // STORM_AUTOTEST=vehicles: walk to vehicles and drive them
    std::string backendUrl = "http://127.0.0.1:8080";
    std::string lastUser;
};

struct RemotePlayer {
    uint16_t id = 0;
    std::string name;
    uint8_t team = 0;
    bool bot = false;
    Loadout loadout;
    struct Sample { float t; SnapPlayer s; };
    std::deque<Sample> buf;
    SnapPlayer cur;          // interpolated
    bool present = false;    // in latest snapshot
    float animTime = 0;
    float speed = 0;
    si::Vec3 lastPos;
    float swing = 0;
    float stepTimer = 0;
    std::vector<si::Vec3> trail;
};

struct ClientVehicle {
    uint16_t id = 0;
    struct Sample { float t; SnapVehicle s; };
    std::deque<Sample> buf;
    SnapVehicle cur;         // interpolated
    bool present = false;
    bool init = false;
    float speed = 0;
    float wheelSpin = 0;
    float steer = 0;
    si::Vec3 lastPos;
    Basis ballSpin;
};

struct ClientItem { uint32_t id; ItemStack stack; si::Vec3 pos; float spawnTime; };
struct Tracer { si::Vec3 a, b; float t; Color c; };
struct Flash { si::Vec3 p; float t; };
struct Blast { si::Vec3 p; float t; float r; };
struct Particle { si::Vec3 p, v; float t, life; Color c; float size; };
struct DamageNumber { si::Vec3 p; int amount; uint8_t flags; float t; };
struct KillFeedEntry { std::string text; float t; bool mine; };
struct Message { std::string text; uint8_t kind; float t; };
struct DamageIndicator { si::Vec3 from; float t; };

struct MatchResult {
    bool has = false;
    int placement = 0;
    int kills = 0;
    float damage = 0;
    int players = 0;
    bool won = false;
    bool final = false; // match fully ended (vs. just eliminated)
};

class GameClient {
public:
    GameClient(NetClient& net, Settings& settings, AudioSystem& audio);
    ~GameClient();
    // Returns false when the player wants to leave the match.
    bool frame(float dt);
    bool leaveRequested() const { return leave_; }
    const MatchResult& result() const { return result_; }
    bool ready() const { return mapReady_; }

private:
    NetClient& net_;
    Settings& settings_;
    AudioSystem& audio_;
    GameMap map_;
    CollisionWorld world_;
    StructureSet structures_;
    Lighting light_;
    WorldRenderer worldR_;
    bool mapReady_ = false;
    bool leave_ = false;

    // replicated state
    std::map<uint16_t, RemotePlayer> players_;
    std::map<uint32_t, ClientItem> items_;
    std::map<uint16_t, ClientVehicle> vehicles_;
    std::vector<uint8_t> chestOpened_, ammoBoxOpened_;
    std::vector<si::Vec3> launchPads_;
    struct Drop { si::Vec3 pos; float groundY; bool opened; };
    std::map<uint32_t, Drop> drops_;
    std::vector<SnapProjectile> projectiles_;
    SnapGlobal g_;
    SnapSelf self_;
    bool haveSelf_ = false;
    uint32_t eventAck_ = 0;
    float serverTime_ = 0, renderTime_ = 0;
    bool timeInit_ = false;
    MatchResult result_;

    // prediction
    MoveState pred_;
    bool predInit_ = false;
    std::deque<InputCmd> pendingInputs_;
    std::vector<InputCmd> recentInputs_;
    uint32_t inputSeq_ = 0;
    std::vector<ClientAction> pendingActions_;
    uint16_t actionSeq_ = 0;
    float accum_ = 0;
    si::Vec3 smoothOffset_;
    // vehicle prediction (while driving)
    VehicleState predVeh_;
    bool driving_ = false;
    uint16_t drivingId_ = 0xFFFF;
    VehicleType drivingType_ = VehicleType::GolfCart;
    si::Vec3 vehSmooth_;
    float vehYawSmooth_ = 0;
    float vehSteerVis_ = 0;
    float vehWheelSpin_ = 0;
    Basis vehBallSpin_;
    bool vehLanded_ = false;
    // vehicle autotest autopilot
    uint16_t apTarget_ = 0xFFFF;
    float apDrive_ = 0, apStuck_ = 0;
    si::Vec3 apLastPos_;
    uint32_t apDoneTypes_ = 0;
    float apTargetTime_ = 0;
    std::vector<uint16_t> apSkip_;
    void autopilotVehicles(InputCmd& in);
    float camYaw_ = 0, camPitch_ = 0;
    uint16_t buttonsHeld_ = 0;
    bool jumpQueued_ = false;
    float time_ = 0;

    // ui state
    bool mapOpen_ = false, inventoryOpen_ = false, paused_ = false, emoteWheel_ = false;
    int invDragSlot_ = -1;
    float hitMarker_ = 0;
    bool hitMarkerHead_ = false;
    float damageFlash_ = 0;
    std::vector<Tracer> tracers_;
    std::vector<Flash> flashes_;
    struct Cloud { si::Vec3 pos, size; float speed; };
    std::vector<Cloud> clouds_;
    float stepTimer_ = 0;
    int lastSlotSound_ = -1;
    float shake_ = 0;
    std::vector<Blast> blasts_;
    std::vector<Particle> particles_;
    std::vector<DamageNumber> dmgNumbers_;
    std::vector<KillFeedEntry> killfeed_;
    std::vector<Message> messages_;
    std::vector<DamageIndicator> dmgIndicators_;
    Camera3D cam_{};
    int lastSelected_ = -1;
    uint8_t lastPhase_ = 255;
    float stormTickSound_ = 0;
    std::string interactPrompt_;
    // HUD: location banner when entering a named place
    std::string poiName_;
    float poiBanner_ = 0;
    void hudCompass();
    void hudTopRight();
    void hudLocationBanner(float dt);

    void initMap(uint32_t seed);
    void applySnapshot(const Snapshot& s);
    void handleEvent(const std::vector<uint8_t>& ev);
    void pushAction(ActionType t, uint8_t a = 0, uint8_t b = 0);
    void sampleInput(float dt);
    MoveEvents stepPrediction(const InputCmd& in);
    void reconcile(const Snapshot& s);
    void updateInterpolation(float dt);
    void updateEffects(float dt);
    void computeInteractPrompt();
    void updateVehicleAudio(float dt);
    // Current visual state of a vehicle (predicted when we drive it, interpolated otherwise).
    bool vehicleVisual(uint16_t id, VehicleVisual& out) const;
    bool inVehicle() const { return haveSelf_ && self_.vehicle != 0xFFFF && (self_.flags & PF_ALIVE); }
    si::Vec3 viewPos() const;
    bool localControllable() const;
    const RemotePlayer* spectateTarget() const;

    // rendering
    void render3D();
    void renderPlayers();
    void renderStructures();
    void renderItems();
    void renderStorm();
    void renderBus();
    void renderVehicles(bool shellPass);
    void renderEffects();
    void renderBuildPreview();
    void renderSlipstreams();
    // HUD (hud.cpp)
    void renderHud();
    void hudBottom();
    void hudMinimap(Rectangle r, float zoomMeters);
    void hudFullMap();
    void hudKillfeed();
    void hudCenter();
    void hudInventory();
    void hudEmoteWheel();
    void hudPause();
    void hudResults();
    void hudVehicle();
    Vector2 worldToMap(const si::Vec3& p, Rectangle r, si::Vec3 center, float meters, bool whole) const;
    std::string playerName(uint16_t id) const;
};

} // namespace client

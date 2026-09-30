# Storm Island

A third-person battle royale with building, written in C++17. You drop from an airship onto an island, loot, harvest, build, fight up to 99 players or bots, and outlast the storm.

It ships as three programs:

| Program | What it does |
|---|---|
| `stormisland` | Game client (raylib/OpenGL): login, lobby, locker, item shop, career, matchmaking, and the game itself |
| `stormisland-server` | Authoritative dedicated game server (UDP) with server-side bot AI |
| `stormisland-backend` | HTTP backend: accounts, lockers, item shop, stats, friends, matchmaking, game-server orchestration and an **admin panel** |

Most art is procedural low-poly geometry and every location and most cosmetics are original designs. Vehicles, some props, the "Arena Trooper" outfit, blaster models and many sound effects are **CC0 (public domain) assets by [Kenney](https://kenney.nl)**, listed in `client/assets/CREDITS.md`. Every file-based asset is optional: if `assets/` is missing the game falls back to its procedural models and synthesized sounds. The game is inspired by the genre; it contains no assets or trademarks from other games.

## Features

**Gameplay**
- 1.5 km procedurally built island, generated the same way from a seed on the client and the server. 18 named locations plus landmarks: a futuristic glass-tower city in the middle, a mall, an industrial forge, a pirate cove with a ship, a snowy mountain lodge and airfield, a desert oasis town, a lantern village with a pagoda, a farm, suburbs, a jungle step-pyramid, a haunted mansion, a junkyard, seaside estates, canyon mines, a lake with an island house, and a volcano with launch vents.
- Biomes: grassland, forest, snow, desert, jungle, volcanic and beach, with roads, cars, trees, rocks, cacti and crops.
- Slipstream wind tunnels, volcano vents, launch pads, gliding and redeploy after launches.
- Drop-ship phase, skydiving (with a dive), glider deploy, swimming and fall damage.
- Storm with 8 shrinking phases, storm damage and a supply drop on some phases.
- Chests, ammo boxes, floor loot and supply drops. 5 rarities, 10 guns (AR, burst, scoped, pump, tactical, SMG, pistol, bolt sniper, rocket launcher, minigun), grenades, impulse grenades, bandages, medkits, small and large shields, regen soda, mega flask and launch pads.
- Hit-scan weapons with bloom, first-shot accuracy, headshots, damage falloff, pellets, burst fire and ADS zoom. Rockets and grenades are simulated projectiles.
- Harvesting wood, brick and metal with the pickaxe. Every map prop is destructible.
- **Building:** walls, floors, ramps and roofs in three materials. Pieces grow in health while they build. Edits: door, window and arch walls, holed floors, and ramp rotation. Structural collapse when support is destroyed.
- Solo, Duos and Squads. Teams get down-but-not-out (knocked) states, revives, and no friendly fire.
- Warmup with respawns, kill feed, damage numbers, hit markers, damage direction indicators and spectating.
- 5-slot inventory with swapping and dropping, a full map, a minimap, 6-slot emote wheel, contrails and gliders.

**Networking**
- Server-authoritative 30 Hz simulation with 60 Hz client input.
- Client-side prediction with reconciliation for your own player, and snapshot interpolation for everyone else.
- Reliable ordered event channel over UDP for world changes such as builds, loot and kills.

**Bots**
- Four difficulties. Bots pick drop spots, loot chests and weapons, rotate with the storm, heal, choose weapons by range, aim with difficulty-based error, strafe, jump, build cover, and harvest through obstacles when stuck.

**Backend**
- Accounts with PBKDF2-SHA256 password hashing and bearer sessions.
- Lockers: outfit, back bling, pickaxe, glider, contrail and 6 emotes.
- Daily rotating item shop with coins.
- XP and levels. Level-up rewards unlock cosmetics.
- Stats, match history and a leaderboard.
- Friends: requests, accepting and removing.
- Matchmaking queue per playlist. It packs players into open servers and **spawns new game servers automatically**.
- Game servers register, heartbeat and report results. Match tickets are validated by the backend so only matched players can join.

**Admin panel** (`http://<backend>/admin`)
- **Default bots per match** and bot difficulty, or "fill every empty slot with bots".
- Live server list: state, players, bots and alive counts, plus the player list with kick.
- **Add or remove bots on any running server**, start a match now, pause or resume the storm, broadcast a message, end the match, or shut the server down.
- Start new servers per playlist, optionally with bots.
- Game settings: storm speed, warmup length, humans required to start, max players, max servers, message of the day, and a matchmaking on/off switch.
- Player management: search, ban/unban, grant coins, give all cosmetics, grant/revoke admin. Recent matches and an audit log of every admin action.

## Building

Requirements: CMake 3.16+, a C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+) and git. raylib 5.5 is downloaded by CMake. SQLite, cpp-httplib and nlohmann/json are vendored in `third_party/`.

**Linux** (Debian/Ubuntu):
```sh
sudo apt install build-essential cmake git libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**macOS** (Xcode command line tools and CMake):
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**Windows** (Visual Studio 2022 with the C++ workload):
```bat
cmake -S . -B build
cmake --build build --config Release --parallel
```

Everything lands in `build/bin/`. Run the tests with `ctest --test-dir build -C Release`.

Options: `-DSTORM_BUILD_CLIENT=OFF` builds only the server and backend, for example on a headless machine. `STORM_BUILD_SERVER`, `STORM_BUILD_BACKEND` and `STORM_BUILD_TESTS` work the same way.

Prebuilt Linux, Windows and macOS packages come from GitHub Actions (`.github/workflows/build.yml`) on every push. Pushing a `v*` tag publishes them as a GitHub release.

## Running

### Quick start: offline against bots
Start `stormisland`, choose **Play offline against bots**, and pick the bot count, difficulty and mode. The client runs a game server in the background, so nothing else is needed.

### Full online setup
```sh
cd build/bin
./stormisland-backend --admin-password choose-a-password
```
- The backend listens on port 8080. Open `http://localhost:8080/admin` and sign in as `admin`.
- Players start `stormisland`, create an account (the backend URL is on the login screen), and press **PLAY**.
- The backend starts `stormisland-server` processes on demand, using UDP ports 7777–7876. Each spawned server exits after its match.
- For players on other machines, start the backend with `--public-host <your LAN or public IP>` and open the UDP port range in your firewall.

Useful backend options: `--port`, `--db`, `--ports 7777-7876`, `--no-spawn` to only accept externally started servers, and `--secret` for the shared server secret. Without `--secret`, the backend generates one and stores it in the DB. The maximum number of auto-started servers is set in the admin panel.

### Dedicated servers on other machines
```sh
STORM_SERVER_SECRET=<secret> ./stormisland-server --backend http://backend-host:8080 --public-host <this machine's IP> --port 7777 --playlist solo
```
Without `--backend` the server runs standalone and accepts anyone. Players join it with **Direct connect** in the client. Run `./stormisland-server --help` for bot count, difficulty, warmup and seed options.

## Controls
| Key | Action |
|---|---|
| WASD, Space, Shift, Ctrl | Move, jump/glide, sprint, crouch |
| Mouse, LMB, RMB | Aim, fire/place, aim down sights |
| 1–5, F, mouse wheel | Inventory slots, pickaxe |
| E | Interact (hold for chests and revives) |
| R | Reload / rotate the build piece |
| Q, Z, X, C, V | Build mode, wall, floor, ramp, roof |
| RMB (build mode) | Change material |
| G | Edit the targeted piece (door, window, arch, hole, rotate) |
| Tab / M / B | Inventory / map / emote wheel (1–6) |
| Esc | Menu |

## Project layout
```
shared/      math, UDP transport, protocol and snapshot codec, map generator, collision world,
             building rules, character movement, items and cosmetics (used by every program)
server/      match simulation (game.cpp), bot AI (bots.cpp), UDP server, backend link, runner
client/src/  raylib client: app/menus, game client, world and character renderers, HUD, audio
backend/     REST API + matchmaker + server orchestration (src/), admin panel (web/admin.html)
tests/       headless tests: map determinism, building rules, full bots-only matches
third_party/ cpp-httplib, nlohmann/json, SQLite amalgamation (MIT / public domain)
```

## Notes and limits
- Traffic between the client and backend is plain HTTP. Put the backend behind a TLS reverse proxy such as nginx or Caddy before exposing it to the internet.
- Snapshots contain every player, which is fine on a LAN. Over the internet, full 100-player lobbies produce large UDP packets.
- There is no lag compensation for hit-scan, so high-latency players need to lead their targets slightly.

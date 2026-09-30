# Hosting the Storm Island backend

The backend ships as one Docker image. One container runs:

- the HTTP backend: accounts, lockers, item shop, stats, friends, matchmaking and the admin panel at `/admin`
- the game servers: the backend starts a `stormisland-server` process inside the container for each match, and it exits when the match ends

All data (accounts, lockers, match history, settings) lives in one SQLite file under `/data`. Mount a volume there.

## Ports

| Port | Protocol | Purpose |
|------|----------|---------|
| 8080 | TCP | HTTP API and admin panel (`STORM_HTTP_PORT`) |
| 7777–7786 | **UDP** | Game servers, one port per concurrent match (`STORM_PORTS`) |

Both must be reachable by players. Check the UDP range in particular: cloud firewalls and security groups often allow only TCP by default.

## Quick start with Docker Compose

On any Linux server with Docker installed:

```sh
git clone https://github.com/lukeazaz0-cell/fn-clone.git
cd fn-clone
cp .env.example .env
nano .env                   # set STORM_PUBLIC_HOST and STORM_ADMIN_PASSWORD
docker compose up -d --build
```

Then open `http://<your-server>:8080/admin` and sign in as `admin` with the password from `.env`. In the game client, enter `http://<your-server>:8080` as the backend URL on the login screen.

Without `--build`, Compose pulls the prebuilt image from GitHub Container Registry (see [Prebuilt images](#prebuilt-images)).

Useful commands:

```sh
docker compose logs -f          # backend and game-server logs
docker compose pull && docker compose up -d   # update to the newest image
docker compose down             # stop (data stays in the volume)
```

## Plain `docker run`

```sh
docker build -t stormisland-backend .
docker run -d --name stormisland --restart unless-stopped \
  -p 8080:8080 -p 7777-7786:7777-7786/udp \
  -e STORM_PUBLIC_HOST=203.0.113.10 \
  -e STORM_ADMIN_PASSWORD=change-me-please \
  -v stormisland-data:/data \
  stormisland-backend
```

On Linux you can use `--network host` instead of the `-p` flags. This avoids Docker's per-port UDP proxying, which helps with large port ranges.

## Configuration

Every setting is an environment variable. Command-line flags, if given, take precedence.

| Variable | Default (in image) | Meaning |
|----------|--------------------|---------|
| `STORM_PUBLIC_HOST` | `127.0.0.1` | **Required for real hosting.** The IP or DNS name the backend gives game clients for connecting to game servers. Use your server's public address. |
| `STORM_ADMIN_PASSWORD` | – | Password for the admin account. It is applied on every start, so changing it here resets it. |
| `STORM_ADMIN_USER` | `admin` | Name of the admin account. |
| `STORM_HTTP_PORT` / `PORT` | `8080` | HTTP port. `PORT` is honoured for PaaS platforms. |
| `STORM_PORTS` | `7777-7786` | UDP port range for spawned game servers, which also caps concurrent matches. With Compose, the same variable drives the port mapping. |
| `STORM_DB` | `/data/stormisland.db` | SQLite database path. |
| `STORM_SERVER_SECRET` | generated | Shared secret between the backend and game servers. Set it only when you run extra game servers on other machines (below). |
| `STORM_NO_SPAWN` | off | Set to `1` so this container never starts game servers and only accepts external ones. |
| `STORM_BIND` | `0.0.0.0` | HTTP bind address. |

The default bot count, bot difficulty, the maximum number of auto-started servers and other gameplay settings are set in the admin panel and stored in the database.

## Firewall

For example, with `ufw`:

```sh
sudo ufw allow 8080/tcp
sudo ufw allow 7777:7786/udp
```

On AWS, GCP, Azure, Hetzner, DigitalOcean and similar providers, add the same rules to the instance's security group or cloud firewall.

## HTTPS (recommended on the internet)

The backend speaks plain HTTP. To expose it publicly, put a TLS reverse proxy in front of port 8080 and leave the game UDP ports as they are. With [Caddy](https://caddyserver.com/), which gets certificates automatically:

```
play.example.com {
    reverse_proxy 127.0.0.1:8080
}
```

Players then use `https://play.example.com` as the backend URL. In that setup, publish 8080 only on localhost (`-p 127.0.0.1:8080:8080`).

## Backups

The whole state is `/data/stormisland.db`. To back it up from the running container:

```sh
docker run --rm -v stormisland-data:/data -v "$PWD":/backup mirror.gcr.io/library/debian:bookworm-slim \
  cp /data/stormisland.db /backup/stormisland-$(date +%F).db
```

(With Compose, the volume is named `<project>_stormisland-data`, typically `fn-clone_stormisland-data`. Run `docker volume ls` to check.) To restore, stop the container, copy the file back into the volume, and start it again.

## Extra game servers on other machines

The container can also run only game servers, connected to a backend elsewhere:

```sh
docker run -d --network host -e STORM_SERVER_SECRET=<same secret as backend> \
  --entrypoint /app/stormisland-server stormisland-backend \
  --backend http://backend-host:8080 --public-host <this machine's IP> --port 7777 --playlist solo
```

Set the same `STORM_SERVER_SECRET` on the backend container too (otherwise it generates its own and external servers are rejected).

## Prebuilt images

Every push to GitHub builds, tests and publishes the image to GitHub Container Registry:

- `ghcr.io/lukeazaz0-cell/fn-clone-backend:latest` (default branch and release tags)
- `ghcr.io/lukeazaz0-cell/fn-clone-backend:<branch>`, `:<version>` (for `v*` tags) and `:sha-<commit>`

New GHCR packages are private. Either make the package public (GitHub → your profile → Packages → fn-clone-backend → Package settings → Change visibility), or run `docker login ghcr.io` with a personal access token that has `read:packages` on the host.

## PaaS platforms (Railway, Render, Fly.io, …)

The HTTP part works anywhere that runs a Dockerfile, and `PORT` is honoured. However, game servers need **inbound UDP**, which most PaaS platforms don't offer. Fly.io is an exception, but it needs a dedicated IPv4 and explicit UDP service configuration. A small VPS with Docker is the simplest reliable option.

## Troubleshooting

- **Players can log in but get stuck connecting to a match.** `STORM_PUBLIC_HOST` is wrong, or the UDP ports are blocked. The backend prints a warning at startup if it is still `127.0.0.1`.
- **"no free ports" or matches not starting.** All ports in `STORM_PORTS` are in use. Widen the range (and the port mapping), or raise the server limit in the admin panel.
- **Health check.** `curl http://<host>:8080/health` returns `{"ok":true}`. `docker ps` also shows the container's health status.

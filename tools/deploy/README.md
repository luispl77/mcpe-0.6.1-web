# The multiplayer deployment

`mcpe.continualmi.com` — the same build as github.io, with the two constants in
the page pointed at a lobby, which is the whole difference between single-player
and one big LAN party. See [`../lobby/README.md`](../lobby/README.md) for what
the service does and [`../../CLAUDE.md`](../../CLAUDE.md) for why there is only
one build.

It runs on the Continual MI serving box, which also serves company sites. That
shapes every choice here: **one host process, no container, and nothing added to
that box's `docker-compose.yml`.** The service serves the page as well as the
board and the relay, so the whole deployment is one unit to install, check and
stop, and the only shared file it touches is the Caddyfile.

Nothing here installs itself.

## Once

```sh
# The service, as a host process like emwaver
sudo mkdir -p /opt/mcpe-lobby /var/www/mcpe
sudo cp tools/lobby/server.js /opt/mcpe-lobby/
sudo cp tools/lobby/mcpe-lobby.service /etc/systemd/system/

# Box-specific settings as a drop-in, so the unit stays a template
sudo mkdir -p /etc/systemd/system/mcpe-lobby.service.d
sudo tee /etc/systemd/system/mcpe-lobby.service.d/box.conf >/dev/null <<'EOF'
[Service]
# Caddy is in a container, so its 127.0.0.1 is not the host's. It reaches host
# processes at the compose bridge gateway -- the same way emwaver is reached.
# Binding the gateway rather than 0.0.0.0 keeps the port off the public
# interface entirely, so a firewall mistake cannot expose it.
Environment=MCPE_LOBBY_HOST=172.18.0.1
Environment=MCPE_WEB_ROOT=/var/www/mcpe
# Safe only because the vhost below overwrites X-Forwarded-For.
Environment=MCPE_LOBBY_TRUST_PROXY=1
ReadWritePaths=
EOF

sudo systemctl daemon-reload && sudo systemctl enable --now mcpe-lobby
curl -s 172.18.0.1:8477/lobby-check 2>/dev/null; curl -s 172.18.0.1:8477/   # says what it is

# Containers reach the host through the INPUT chain, so UFW must allow it.
# Scoped to the bridge: this must never be reachable from the internet directly.
sudo ufw allow from 172.18.0.0/16 to any port 8477 proto tcp \
  comment 'mcpe lobby+relay (Caddy -> host process)'

# The vhost. mcpe.continualmi.com already resolves via the *.continualmi.com
# wildcard record, which currently answers 404 from the wildcard vhost.
sudo cp /opt/continualmi/caddy/Caddyfile /opt/continualmi/caddy/Caddyfile.bak-mcpe
# paste tools/deploy/mcpe.continualmi.com.caddy in above the *.continualmi.com block
docker compose -f /opt/continualmi/docker-compose.yml --profile app restart caddy
```

That restart is a **~2 s blip on every site the box serves**, not just this one.
It is the documented way to reload a Caddyfile there — Caddy runs with
`admin off` — so do it deliberately rather than as an afterthought.

## Every time Pages deploys

```sh
sudo tools/deploy/sync-from-pages.sh
```

It fetches what github.io is serving and rewrites `LOBBY_URL` and `RELAY_URL` in
`index.html`. Pages is the build artifact store — the workflow there is the only
thing that compiles, and taking its bytes verbatim is what makes "one build, two
deployments" true rather than aspirational.

**The corollary is that this box is only ever as new as Pages.** Pushing without
dispatching `pages.yml` leaves both deployments on the old commit, and running
this before the run finishes silently reinstalls the old one.

No restart is needed — the files are read per request, and `Cache-Control:
no-cache` with an mtime ETag means every browser revalidates and picks the new
build up on its next load.

## Checking it, rather than assuming

A green run and a reloaded Caddy are claims about a build and a config. These
are the site:

```sh
# the page is being served, and it knows where its lobby is
curl -s https://mcpe.continualmi.com/ | grep -o "var RELAY_URL = '[^']*'"

# the board answers through the proxy
curl -s https://mcpe.continualmi.com/lobby/ | python3 -m json.tool

# the upgrade gets through -- the one that is easy to miss, because the list
# fills and the game looks fine right up until nobody can be joined
curl -sSi -o /dev/null -w '%{http_code}\n' \
  -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' -H 'Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==' \
  https://mcpe.continualmi.com/lobby/relay        # want 101
```

`200` means Caddy answered instead of proxying; `400` means the service saw a
request that was no longer an upgrade; `502` means the service is down or UFW is
not letting the bridge through.

## Turning multiplayer off

`sudo systemctl stop mcpe-lobby`. The page goes with it, because the same
process serves it — so this is "the deployment is off", not "the game is
broken". github.io is unaffected and is where the standing single-player copy
lives. Nothing persists, so starting it again needs no recovery.

To take only multiplayer off and leave the page up, set `RELAY_URL` back to `''`
in `/var/www/mcpe/index.html`; the board and the page keep working and joining
stops being offered.

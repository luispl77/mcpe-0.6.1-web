# The multiplayer deployment

`mcpe.continualmi.com` — the same build as github.io, with the two constants in
the page pointed at a lobby, which is the whole difference between single-player
and one big LAN party. See [`../lobby/README.md`](../lobby/README.md) for what
the service does and [`../../CLAUDE.md`](../../CLAUDE.md) for why there is only
one build.

Nothing here installs itself. These are the files and the order.

## Once

```sh
# The service
sudo mkdir -p /opt/mcpe-lobby
sudo cp tools/lobby/server.js /opt/mcpe-lobby/
sudo cp tools/lobby/mcpe-lobby.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now mcpe-lobby
curl -s localhost:8477            # says what it is

# TLS, then the vhost
sudo certbot certonly --nginx -d mcpe.continualmi.com
sudo cp tools/deploy/mcpe.continualmi.com.nginx /etc/nginx/sites-available/mcpe
sudo ln -sf /etc/nginx/sites-available/mcpe /etc/nginx/sites-enabled/mcpe
sudo nginx -t && sudo systemctl reload nginx
```

The unit binds to `127.0.0.1`, so nginx is the only way in. Add
`Environment=MCPE_LOBBY_TRUST_PROXY=1` to it **only** with the vhost above in
front, which overwrites `X-Forwarded-For` rather than appending to it.

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

## Checking it, rather than assuming

A green run and a reloaded nginx are claims about a build and a config. These
are the site:

```sh
# the page is the new one, and it knows where its lobby is
curl -s https://mcpe.continualmi.com/ | grep -o "var RELAY_URL = '[^']*'"

# the board answers through the proxy
curl -s https://mcpe.continualmi.com/lobby/ | python3 -m json.tool

# the upgrade gets through -- this is the one that is easy to miss, because
# everything else works when it does not
curl -sSi -o /dev/null -w '%{http_code}\n' \
  -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' -H 'Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==' \
  https://mcpe.continualmi.com/lobby/relay        # want 101, not 400 or 200
```

A `200` there means nginx answered instead of proxying; a `400` means the
service saw a request that was no longer an upgrade. Either way the Join Game
list will still fill and joining will never work, which is the failure this
deployment is most likely to have and the least likely to notice.

## Turning multiplayer off

`systemctl stop mcpe-lobby`. The page degrades to an empty Join list, which is
what github.io shows on purpose. Players mid-game are dropped and see each other
time out. Nothing persists, so starting it again needs no recovery.

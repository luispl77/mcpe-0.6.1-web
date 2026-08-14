# mcpe-lobby

The board and the wire, for web multiplayer. One Node file, no dependencies.

0.6.1 does two things over the LAN that a browser tab cannot. It finds games by
broadcasting to `255.255.255.255` on 19132-19135 and filling the list from
whoever pongs back; and it then talks to whoever you picked over UDP. A tab has
neither the broadcast nor the socket, so this service is both:

- **the board** — a tab says *I am playing* while it plays, and asks who else is
- **the wire** — `/relay` forwards datagrams between tabs, so the players on the
  board can actually be joined

They are one process because the alternative is two Node runtimes, and the
runtime is almost the whole cost. See [What it costs](#what-it-costs).

## Running

```sh
node tools/lobby/server.js
curl -s localhost:8477 | python3 -m json.tool
```

`MCPE_LOBBY_PORT` (8477), `MCPE_LOBBY_HOST` (127.0.0.1), `MCPE_LOBBY_TTL`
(35000 ms), `MCPE_RELAY_MAX` (200 sockets) and `MCPE_RELAY_RATE` (512 KB/s per
socket) are the only knobs. `GET /` describes the service to whoever finds the
port open months from now.

`mcpe-lobby.service` beside this file is a systemd unit template. Nothing in
this repo installs it — copy it to `/etc/systemd/system/` on whichever box is
going to host it. It binds to localhost, so putting it on the public internet is
a separate, deliberate step: a reverse proxy in front terminating TLS, because a
browser will not let an https page open a `ws://` socket or fetch `http://`.

| Route | |
|---|---|
| `GET /` | what this is, how many are on it, how many sockets it holds |
| `GET /list` | `{players: [{id, name, world, route, age, dedicated?, locked?}]}` |
| `POST /announce` | `{id, name, world, route, token}`, or `{id, gone: true}` |
| `POST /unlock` | `{route, token, server, password}` — opens a locked server |
| `GET /relay` (upgrade) | the datagram switch |

## Dedicated servers

A world that is not anybody's tab. `MCPE_LOBBY_SERVERS` points at a JSON file:

```json
[{"id": "skyblock", "name": "Skyblock", "world": "creative",
  "host": "127.0.0.1", "port": 19150,
  "salt": "…", "passwordHash": "sha256(salt + password), hex"}]
```

It is read at startup and on **SIGHUP** — a reload rather than a restart,
because restarting would drop every player already in a world. Absent or
unparseable means there are no dedicated servers and everything else works as
before, the same way an absent relay means no multiplayer rather than an error.

A server holds a reserved route and sits in the switching table beside the tabs,
so **a player addresses it exactly the way they address another player**, and
the game does not need to know the difference — an unmodified client lists and
joins one already. Its route is stable across reloads as long as its `id` is.

What is behind the route is UDP on this box, and the bridge holds **one socket
per (server, player)** rather than one per server. That is not thrift, it is
correctness: the game keys remote systems by `SystemAddress`, which is address
*and* port, so a shared socket would present every player as the same peer and
the second one to connect would look like the first having a very strange time.
Distinct ephemeral ports give the server distinct peers without the bridge
forging anything — which it could not do without raw sockets and root anyway.

`MCPE_BRIDGE_MAX` (400 sockets) and `MCPE_BRIDGE_IDLE` (60000 ms) bound that
product. Idle expiry matters because the event that should close a bridge socket
— a player leaving — arrives as a WebSocket close, and RakNet never says a word
about it.

Servers are counted separately from tabs everywhere it matters. They live in the
routing table but they are not connections, so they do not show up in
`connected` and do not consume `MCPE_RELAY_MAX`; registering one must not
quietly lower how many people can be in a world.

### Passwords

`passwordHash` is `sha256(salt + password)` in hex, and empty means the server is
open. The gate is **here, not in the game**: a locked server's datagrams are
dropped by the switch until that socket has posted the right password to
`/unlock`, so being refused costs a stranger a datagram rather than a seat, and
0.6.1's protocol — whose `LoginPacket` carries a name and two version numbers and
nothing else — does not have to grow a field it never had.

The unlock is bound to the relay socket that asked for it, by the same token
pairing `/announce` uses. One player knowing the password does not open the
server for anybody else, and nothing about it survives that socket closing.
Attempts are counted per socket rather than per address, because behind a phone
network or a school an address is everybody and locking them all out for one
guesser is the wrong failure.

## The switch

Every tab holds one WebSocket. On connect it is given a **route** — a small
number, unique among connected tabs — and a **token**.

```
client -> [4-byte destination route][payload]
server -> [4-byte source route     ][payload]
```

That substitution on the way out is the whole design. The receiving tab needs to
know who sent it and the sender cannot be trusted to say, so the switch says.
You can address anyone; you cannot claim to be them.

The payloads are opaque. It does not speak RakNet and does not want to — the
game's handshake, ordering and reliability all run end to end between the two
tabs, and the relay is a UDP socket's worth of "best effort, no promises".

A route travels to the other players through the board, which is why `/announce`
takes one. The token is what stops a tab publishing somebody else's: the relay
issued both on the socket the route belongs to, and this is the only process
that knows the pairing. An announce that fails the check is still listed, just
with `route: 0` — on the board, not joinable.

Routes are 24-bit because of what happens at the far end: the game keys remote
systems by IPv4 address, so a route travels as the low three octets of a
`10.0.0.0/8` address. Never 0, which the client reads as "not joinable".

## What it costs

Measured on node v22, RSS from `/proc`:

| | RSS |
|---|---|
| idle | ~61 MB |
| 200 announces on the board | ~73 MB |
| + 200 relay sockets carrying 8,000 × 1404 B | ~85 MB |
| after every socket closed | ~87 MB, switch back to 0 |

So the relay costs about **12 MB on top of the board at 200 concurrent
players**, and the board about 12 MB on top of an empty Node. Nearly everything
here is the runtime: 200 board entries is about 12 KB of strings, and a
connection holds a partial-frame buffer bounded at 4 KB.

The last row is Node not returning heap to the OS rather than anything being
held — `connected` goes back to 0, which the wiretest checks.

That measurement ran everything from loopback, so the per-source cap held the
board at 25 of the 200 announces; the board figure is the work of 200 announces,
not 200 resident entries.

## Behind a proxy

The proxy has to pass the upgrade through, or `/relay` will 400 and multiplayer
will be silently absent while the board looks fine:

```nginx
location / {
    proxy_pass http://127.0.0.1:8477;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection $connection_upgrade;
    proxy_set_header X-Forwarded-For $remote_addr;   # not $proxy_add_x_forwarded_for
    proxy_read_timeout 3600s;                        # a relay socket is long-lived
}
```

When you check that the upgrade really does pass, **force HTTP/1.1**:

```sh
curl -s --http1.1 --max-time 8 -o /dev/null -w '%{http_code}\n' \
  -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  https://host/lobby/relay
```

Without `--http1.1` curl negotiates h2, where `Connection` and `Upgrade` are not
valid headers and are dropped — so the request arrives as an ordinary GET and
the service answers `404`, which looks exactly like a proxy that is eating the
upgrade. `--max-time` matters too: a real `101` holds the socket open, so the
success case is a timeout with `101` already printed.

`MCPE_LOBBY_TRUST_PROXY=1` makes it believe `X-Forwarded-For`. Leave it **off**
unless something we control is in front, because the header is client-supplied
and trusting it on a directly-reachable service lets anyone forge a source.

The proxy must *overwrite* the header, not append to it. This matters more than
it looks: the per-source cap counts players per IP, and behind a proxy every
player arrives from the proxy's address — so with the cap at its original 4, the
whole board would have held four players. It is 25 now, because a household, a
school or a phone network is also one IP for everyone behind it. `MAX_PLAYERS`
is what actually bounds memory; the per-source cap only stops one source owning
the board.

A full board evicts the stalest entry, but never one seen within half a TTL — so
ghosts on their way out make room for a real player, while live players are not
pushed off by a flood.

`proxy_read_timeout` is not optional. The default is 60s, and a relay socket
that is quiet for a minute — two players standing still — would be cut.

## Pointing the page at it

Two values at the top of their blocks in `handheld/project/web/shell.html`:

| | |
|---|---|
| `LOBBY_URL` | `https://host` — the board. Empty means no list. |
| `RELAY_URL` | `wss://host/relay` — the wire. Empty means no multiplayer. |

They live in the page rather than the wasm for the same reason `window.mcpeTouch`
does — they are properties of where this is deployed, not of the build, so
changing them does not cost a three-minute CI round trip. **This is what makes
one build serve both deployments**: github.io ships with both empty and is
single-player; mcpe.continualmi.com sets both.

Both must be **https**/**wss**. A browser blocks an https page from fetching
http or opening ws, and it fails silently — an empty list and a join that never
happens, with nothing the player can see. `?lobby=` and `?relay=` override them
for a local build, where the page is http too and the rule does not bite.

## Stopping

Stopping is a supported outcome, not an outage. Every path on the client treats
an unreachable lobby as an empty list and an unreachable relay as no
multiplayer, which is what github.io serves on purpose. Three ways, in
increasing order of permanence:

1. `Ctrl-C` / `systemctl stop`. The board and the switch are two Maps in memory
   and go with it. Players in a world are dropped and see each other time out.
2. Set `RELAY_URL` back to `''` and redeploy the page — multiplayer goes, the
   board stays. Setting `LOBBY_URL` to `''` too takes the whole thing off.
3. Revert. `WebRakNetInstance` is only wired in under `MC_WASM`, and the RakNet
   changes are all behind `#if defined(MC_WASM)`.

Nothing persists: every board entry is worthless 35 seconds after it is written
and a route means nothing once its socket has gone, so there is no database and
a restart loses nothing a heartbeat and a reconnect will not rebuild.

## What it deliberately is not

It is not a game server. No world is simulated here and no chunk is stored — the
tab that opened the world is the server for it, which is what keeps this to one
small process no matter how many worlds are running, and what makes a world
disappear when its host closes the tab.

It is not authenticated, and the board is public by design. Anyone who can reach
it can list who is playing and connect to them, which is what "one big LAN
party" means. The `Visible to other players` option in the game is what decides
whether you are on the board at all.

It does not survive its own restart, protect against a determined flood beyond
the caps above, or carry anything but 0.6.1's datagrams.

# mcpe-lobby

Who is playing right now, for the Join Game card. One Node file, no dependencies.

0.6.1 finds games by broadcasting to `255.255.255.255` on 19132-19135 and
filling the list from whoever pongs back. A browser tab has no UDP socket, so on
the web that list was always empty. This replaces the broadcast: a tab says *I am
playing* while it plays, and asks who else is.

## Running

```sh
node tools/lobby/server.js
curl -s localhost:8477 | python3 -m json.tool
```

`MCPE_LOBBY_PORT` (8477), `MCPE_LOBBY_HOST` (127.0.0.1) and `MCPE_LOBBY_TTL`
(35000 ms) are the only knobs. `GET /` describes the service to whoever finds
the port open months from now.

`mcpe-lobby.service` beside this file is a systemd unit template. Nothing in
this repo installs it — copy it to `/etc/systemd/system/` on whichever box is
going to host the lobby. It binds to localhost, so putting it on the public
internet is a separate, deliberate step: a reverse proxy in front terminating
TLS, because a browser will not let an https page fetch http.

| Route | |
|---|---|
| `GET /` | what this is, and how many are on it |
| `GET /list` | `{players: [{id, name, world, age}]}` |
| `POST /announce` | `{id, name, world}`, or `{id, gone: true}` on the way out |

## What it costs

Measured on node v22, RSS from `/proc`:

| | RSS |
|---|---|
| idle | ~61 MB |
| 200 players (the cap) | ~72 MB |
| after 50,000 announces | ~86 MB, flat |

The curve plateaus rather than climbing — 83 MB at 6k announces, 86 MB at 50k,
decelerating throughout, with the board correctly pinned at 200. Almost none of
that is the lobby: 200 entries is about 12 KB of strings, and `GET /list` at a
full board is an 11.7 KB response. The rest is the Node runtime, so the figure
is roughly what any Node process costs and barely moves with player count.

## Behind a proxy

`MCPE_LOBBY_TRUST_PROXY=1` makes it believe `X-Forwarded-For`. Leave it **off**
unless something we control is in front, because the header is client-supplied
and trusting it on a directly-reachable service lets anyone forge a source.

The proxy must *overwrite* the header, not append to it:

```nginx
proxy_set_header X-Forwarded-For $remote_addr;   # not $proxy_add_x_forwarded_for
```

This matters more than it looks. The per-source cap counts players per IP, and
behind a proxy every player arrives from the proxy's address — so with the cap
at its original 4, the whole board would have held four players. It is 25 now,
because a household, a school or a phone network is also one IP for everyone
behind it. `MAX_PLAYERS` is what actually bounds memory; the per-source cap only
stops one source owning the board.

A full board evicts the stalest entry, but never one seen within half a TTL — so
ghosts on their way out make room for a real player, while live players are not
pushed off by a flood.

## Pointing the page at it

`LOBBY_URL` at the top of the lobby block in `handheld/project/web/shell.html`.
It lives in the page rather than the wasm for the same reason `window.mcpeTouch`
does — it is a property of where this is deployed, not of the build, so changing
it does not cost a three-minute CI round trip.

It must be **https**. The page is served from `github.io`, and a browser blocks
an https page from fetching http, so a bare `http://host:8477` fails silently
with an empty list and no error the player can see. `?lobby=http://localhost:8477`
overrides it for a local build, where the page is http too and the rule does not
bite.

## Stopping

Stopping is a supported outcome, not an outage. Every path on the client treats
an unreachable lobby as an empty list, which is what an empty LAN already looked
like, so the Join card goes back to exactly what it did before any of this
existed. Three ways, in increasing order of permanence:

1. `Ctrl-C` / `systemctl stop`. The board is one `Map` in memory and goes with it.
2. Set `LOBBY_URL` back to `''` and redeploy the page — the client stops calling
   out at all.
3. Revert the commit. `WebLobbyRakNetInstance` is only wired in under `MC_WASM`
   and nothing else references it.

Nothing persists: every entry is worthless 35 seconds after it is written, so
there is no database and a restart loses nothing a heartbeat will not rebuild.

## What it deliberately is not

It is a presence board. It carries **no game traffic** and is not on the path of
anything being played — the players it lists cannot yet be joined.

Joining is a different problem, and a bigger one. It needs the game's packets
relayed (browser → WebSocket → bridge → UDP, websockify-style), which in turn
needs RakNet patched: `RakPeer::Startup` spawns `UpdateNetworkLoop` and a
`RecvFromLoop` per socket, and there are no threads here — `-pthread` wants
SharedArrayBuffer, which wants COOP/COEP response headers, which GitHub Pages
cannot send. Driving `RakPeer::RunUpdateCycle` from the game loop instead is the
real work, and none of it is in this directory.

## The crash this also fixed

Opening the Join card used to kill the page, and it was the same root cause.
`RakNetInstance::pingForHosts` calls `RakPeer::Startup` and drops the return
value, then pings regardless. `Startup` cannot succeed in a browser, and
`RakPeer::Ping` then does

```cpp
RakAssert(connectionSocketIndex < socketList.Size());
//	if ( IsActive() == false )
//		return;
    ... socketList[realIndex]->boundAddress ...
```

with the assert compiled out in release and the guard below it commented out in
2013 — so it indexes an empty `socketList` and traps. The web build no longer
constructs a `RakNetInstance` at all; see `handheld/src/network/WebLobbyRakNetInstance.h`.

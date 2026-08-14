# mcpe-servers

Makes worlds that outlive the tab that asked for one. One Node file, no
dependencies, same as the lobby.

The lobby is the board and the switch. This is the only thing that starts a
process or owns a directory, and the two talk through **a file and a signal**
rather than through each other. That split is the point: the lobby is reachable
from the internet and spends its day handling datagrams from strangers, and it
should not also be the thing that can spawn.

```sh
MCPE_SERVER_BIN=/opt/mcpe/mcpe_server \
MCPE_SERVERS_ROOT=/var/lib/mcpe-worlds \
MCPE_SERVERS_FILE=/var/lib/mcpe-worlds/servers.json \
MCPE_LOBBY_PIDFILE=/run/mcpe-lobby.pid \
node tools/servers/manager.js
```

| Route | |
|---|---|
| `GET /` | what this is, how many worlds are up |
| `GET /list` | `{servers: [{id, name, world, locked, upSeconds, idleSeconds}]}` |
| `POST /create` | `{name, password?}` → `{id, name, locked}` |
| `POST /seen` | `{id}` — says somebody is playing, so it is not reaped |
| `POST /stop` | `{id, key}` — operator only |

It writes `MCPE_SERVERS_FILE` and sends the lobby a `SIGHUP`, which is a reload
and not a restart: adding a world must not disconnect the people already in one.
The file is written to a temp name and renamed, because the lobby treats an
unparseable file as "no dedicated servers" and a half-written one would take
every running world off the board at once.

## Why it supervises rather than writes systemd units

It owns its children directly, so it needs **no privilege at all** — no sudo
rule, no polkit policy, nothing that can be turned into more than it was meant
to be. A `systemctl start mcpe@<name>` design would need one of those, and the
thing being granted the privilege is the thing taking requests from the
internet.

The cost is that worlds go down when this does. They go down *cleanly* —
`SIGINT` is what `main_dedicated` traps to save the level — and it writes its
state as it goes, so nothing is lost but uptime.

## The ceilings, and what they are actually for

Anyone who can reach the page can reach `/create`, so each of these is the
difference between a feature and a way to fill a disk:

| | |
|---|---|
| `MCPE_MAX_SERVERS` | 8 worlds at once |
| `MCPE_PORT_FIRST` / `LAST` | 19150–19199, fixed so the firewall rule can be too |
| `MCPE_CREATE_PER_HOUR` | 6 per source — a brake, not a boundary |
| `MCPE_SERVER_IDLE` | 6 h with nobody playing, then it is stopped |

Idle reaping is what actually keeps the box from filling up, and it is the only
one of these a normal player will ever notice. `/seen` is what holds it off;
anybody can call it, which is fine — the worst that buys is a world staying up
that could have gone down, never one going down under a player.

**There is no authentication on `/create`.** That is a deliberate choice for a
LAN party, bounded by the caps above rather than by identity, and it is the
thing to revisit first if this is ever pointed at something that matters: put
Cloudflare Access in front of `/create` and the rest of the design is unchanged.

## Names and ids

The **name** is shown to other players and is used for nothing else. The **id**
is derived from it — lowercased, `[a-z0-9-]`, deduplicated with a counter — and
is the only thing that ever reaches the filesystem. So a name can be anything
printable without the filesystem having an opinion about it, and nothing the
caller writes can climb out of `MCPE_SERVERS_ROOT`.

Processes are spawned with an argv array and never a shell string, so a name is
a name and not a second command.

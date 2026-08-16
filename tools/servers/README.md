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
| `GET /list` | `{servers: [{id, name, world, mode, locked, upSeconds, idleSeconds}]}` |
| `POST /register` | `{user, pass}` → `{user, token}` — makes an account |
| `POST /login` | `{user, pass}` → `{user, token}` |
| `POST /whoami` | `{token}` → `{user, servers: [id], max}` — still signed in, and what it owns |
| `POST /create` | `{name, mode?, seed?, password?, token?}` → `{id, name, locked, owner, ownerAccount}` |
| `POST /configure` | `{id, token\|owner\|key, name?, password?}` — rename, set or clear the password |
| `POST /delete` | `{id, token\|owner\|key}` — stops it and moves its directory aside |
| `POST /seen` | `{id}` — says somebody is playing; also wakes a sleeping world |
| `POST /stop` | `{id, key}` — operator only; puts it to sleep, the next join wakes it |

## Accounts

A name and a password and nothing else. No email, no recovery, no profile: this
is a login for the server list on a Minecraft build from 2013, and every field
that is not here is a field that cannot leak.

It is **only** the server list. Single player and hosting your own tab work
signed out and are never asked about. **Joining a multiplayer world asks**
(since 2026-08-16): the game routes a signed-out Join to the sign-in screen,
and the join then carries the account name — which matters because the server
keys everything on the player's name, most recently the per-player save file,
and signed out every tab is the same "steve" sharing one position and one
inventory. That gate lives in the game, not here: the relay still switches
datagrams for whoever has a route, and a rebuilt client could still claim any
name it likes. The threat model is friends on phones, same as the rest of this.

Passwords are **scrypt** with a per-account salt. Tokens are **signed, not
stored**: `<user>.<expiry>.<hmac>`, where the HMAC covers the account's password
hash as well, so a restart of this process does not sign everybody out and a
password change invalidates every token ever issued. The cost of that trade is
that there is no server-side logout — signing out drops the token in the
browser, and on a shared machine the real answer is to change the password.

State lives in `MCPE_ACCOUNTS_FILE` (default `$MCPE_SERVERS_ROOT/accounts.json`),
written `0600`. It holds the token-signing secret, so **it is the one file here
worth protecting**: with it you can mint a token for any account.

| Knob | Default | |
|---|---|---|
| `MCPE_MAX_ACCOUNTS` | 200 | |
| `MCPE_WORLDS_PER_ACCOUNT` | 3 | how many worlds one account may have up |
| `MCPE_LOGIN_PER_HOUR` | 30 | per source, a brake and not a boundary |
| `MCPE_REGISTER_PER_HOUR` | 5 | |

`/login` says "wrong name or password" whether the name exists or not. `/register`
is specific ("that name is taken", "password: at least 6 characters"), because
those are rules about what you typed rather than hints about who else exists —
and the game shows that sentence verbatim, so it has to be worth reading.

## Who may change a world

A world is owned **one way or the other and never both**:

- **By an account**, when `/create` was given a token. Nothing is handed back;
  ownership is a row in `accounts.json` and follows the person to any device
  they sign in on.
- **By a browser**, when it was not. `/create` mints an **owner secret**, hands
  it back exactly once, and keeps only its SHA-256; the page stores it in
  `localStorage`. This is what ownership was before accounts, and it stays
  because every world made before today is owned this way and none of them
  should stop working.

The operator key works on both. The game asks `canManageServer()` before it
draws Settings and Delete at all, so a world somebody else made has no button
rather than a button that answers 403.

The game requires a sign-in before **New Server**, which this service does not:
it still makes worlds for anyone who asks. That is deliberate — the rule is
about what is worth offering, not about what is permitted, and enforcing it here
would be the thing that broke the worlds that already exist.

Two consequences worth knowing:

- **A world made before accounts has no owner and can only be reached with the
  operator key.** A visitor cannot adopt one, deliberately — a route that handed
  out an unowned world would hand it to whoever asked first. An *operator* can,
  by passing `ownerAccount` to `/configure` along with the key, which is a person
  deciding rather than a race.
- **Clearing the browser's storage loses a world made signed out.** It keeps
  running and stays joinable; nobody can rename or delete it but an operator.
  This is the whole reason accounts exist.

`/delete` **archives rather than removes**: the world is stopped and its
directory renamed to `.deleted-<id>-<stamp>`, still under `MCPE_SERVERS_ROOT`.
The dot prefix keeps it out of the id space so `restore()` never brings it back.
A world is the one thing here that cannot be rebuilt from the repo, and an
operator can sweep the leftovers up whenever.

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
| `MCPE_SERVER_IDLE` | 6 h with nobody playing, then it goes to sleep |

**Idle means asleep, never gone.** A quiet world's process is stopped (SIGINT,
so it saves) but its record stays in the servers file marked `up: false`, so
the board keeps listing it and its port stays reserved. The next datagram at
its route wakes it: the lobby's switch POSTs `/seen`, the manager starts the
process again (~1 s), and RakNet's connection retries carry the join across
the gap. The same rule catches a crash — an exit the manager did not ask for
leaves the world on the board, asleep, and the next join tries it again (with
a ten-second floor so a world that dies at startup is not forked in a loop).

Only `/delete` removes a world from the record. This is not a nicety: on
2026-08-14 reaping *was* deregistration, and six idle hours emptied the whole
board while every directory sat intact underneath it.

`/seen` is fed by the lobby, which is the thing that can actually see traffic:
once a minute per active world, and immediately for a sleeping one. Anybody
else can call it too, which is fine — the worst that buys is a world staying
up that could have gone down, never one going down under a player.

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

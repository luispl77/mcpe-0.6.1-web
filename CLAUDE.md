# Working on this repo

A web build of Minecraft Pocket Edition 0.6.1, running as WebAssembly through
Emscripten and published to GitHub Pages. It is Luís's personal project, not
company work: nothing about it belongs in Continual HQ, PLANNING.md, or any
client-facing tracker.

## Publish immediately. Do not ask.

**Every push is followed by a deploy, in the same breath:**

```sh
git push origin main
gh workflow run pages.yml --ref main
```

This is a standing instruction from the repo's owner (2026-08-12: *"always
publish immediately"*). It is not a per-change approval to go and fetch, and
asking for one is the thing it exists to stop. Push, dispatch, then say in
`#mcpe-web` that it is live.

The workflow is `workflow_dispatch:`-only, so **a push on its own publishes
nothing** — the dispatch is what makes it real, and forgetting it leaves the
site on the previous commit while everything looks done. A run takes ~3 minutes.

**Check the live site, not the green tick.** A successful run is a claim about a
build, not about what the CDN is serving:

```sh
gh run list --workflow=pages.yml --limit 1 --json databaseId,status,conclusion,headSha
curl -sI https://luispl77.github.io/mcpe-0.6.1-web/minecraftpe.wasm | grep -i last-modified
```

A `Last-Modified` from minutes ago is the evidence; so is grepping the served
page for a string that only exists in the new commit.

## Pushing

The remote is SSH and `~/.ssh/id_ed25519_github` already authenticates as
`luispl77`, who owns the repo. There is no token and no deploy key, and HTTPS
will not work — there is no credential helper on this box.

**Never run `gh auth switch`.** `~/.config/gh/hosts.yml` is shared by every
Claude session on this machine, and switching accounts here changes the git
identity under all of them. `luispl77` is already active.

## Building

Needs [emsdk](https://emscripten.org/docs/getting_started/downloads.html),
pinned to **6.0.6** — `LEGACY_GL_EMULATION` is a lightly maintained corner of
Emscripten and the build is not worth letting drift.

```sh
source ~/emsdk/emsdk_env.sh
cd handheld/project/web
emcmake cmake -S . -B build
cmake --build build -j8
cd build && python3 -m http.server 8000   # http, not file://
```

emsdk **is** installed on the dev box now, at `~/emsdk` (this file used to say it
was not, and CI was the compile check). The box is shared and has been OOM-killed
by overlapping heavy jobs before, so run both steps through
`/home/dev/continualmi/infra/watch/heavy.sh`.

`file(GLOB_RECURSE)` is evaluated at configure time, so **a new source file needs
a re-run of `emcmake cmake`**, not just `cmake --build`. The symptom is an
undefined symbol for a class you just added.

### Checking a change without a full build

A screen usually breaks in one of two ways, and neither needs three minutes:

```sh
# Does it parse? Native g++, no emsdk. The game includes <gl/glew.h> and
# <gl/GL.h> Windows-style, so point it at a directory holding two one-line
# shims that include <GL/glew.h> and <GL/gl.h>. Needs libgl-dev, libegl-dev.
g++ -fsyntax-only -w -DMC_WASM -DMC_SDL2 -DMC_DATA_DIR='"/data"' \
    -I handheld/src -I handheld/project/lib_projects/raknet/jni/RaknetSources \
    -I <shim-dir> handheld/src/client/gui/screens/YourScreen.cpp
```

Does it *work*? Drive the built page with Playwright and read the screenshots —
a GUI layout is one of the things that cannot be checked by reading it, because
the coordinate space is ~320×180 on desktop web and ~342×192 on touch. Two
traps: entry to the game is behind `#playBtn` on the touch layout, and
`page.touchscreen.tap` is instantaneous where the game samples the pointer
across frames, so use CDP `Input.dispatchTouchEvent` with a ~260 ms hold.

The board and the services can be stubbed rather than run. `window.mcpeLobby` is
a plain object the wasm calls into, so replacing it after load puts any server
list you like on the screen — `name \t world \t route \t flags`, flags bit 0
dedicated and bit 1 locked. `?servers=`, `?lobby=` and `?relay=` point a local
build at local services.

## Two deployments, one build

| | |
|---|---|
| `luispl77.github.io/mcpe-0.6.1-web` | single-player, from `pages.yml` |
| `mcpe.continualmi.com` | the LAN party, from the Hetzner serving box |

**The wasm is identical.** What differs is two constants at the top of their
blocks in `shell.html` — `LOBBY_URL` and `RELAY_URL` — for the same reason
`window.mcpeTouch` lives there: they are properties of where this is deployed,
not of the build. Empty means single-player, and every path degrades to that
rather than erroring, which is what Pages ships.

So **do not fork the build to turn multiplayer on or off**, and do not add a
compile flag for it. A second wasm is a second thing to keep in step, and the
whole point of the seam is that github.io and the LAN party are the same binary
with a different page around it.

Multiplayer needs all three of: the relay reachable over `wss:`, the board over
`https:`, and a proxy that passes the upgrade through. Miss the last one and the
list still fills while joining silently never happens — see `tools/lobby/README.md`.

The three things RakNet assumes and a tab has not — UDP, threads, broadcast —
are handled in `WebRakNetInstance` and behind `#if defined(MC_WASM)` in
`RakPeer.cpp`/`SocketLayer.cpp`. Two traps live there:

- `RunUpdateCycleOnce()` must be called with **real time between calls**. The
  update cycle decides what to send from how long it has been, so a tight loop
  of four is one frame counted four times, not four frames. That is why
  `disconnect()` spreads its flush over 40ms rather than just calling it.
- `handheld/src/raknet/` and `handheld/project/lib_projects/raknet/` are two
  copies of RakNet and were byte-identical. The web build compiles the second;
  the header the game includes comes from the first. **Patch both.**

`tools/wiretest/` covers the parts of this that cannot be checked by reading
them. Run `raknet.cpp` after touching anything under `#if defined(MC_WASM)`.

## How the game decides it is on a phone

The page works it out **before the module loads** — a coarse primary pointer
with no hover — sets `window.mcpeTouch`, and `main_sdl.h` reads that back into
`g_touchscreen`. One source of truth, because the page needs the same answer for
its own layout and two detections that disagree are worse than one. Force it
either way with `?touch=1` or `?touch=0`; that is also how you reproduce a
phone-only bug in a desktop browser, alongside devtools device emulation.

## Input, and the trap in it

Three input paths come off `IInputHolder`, and **they are consumed in three
different places**. This matters because a bug in one leaves the other two
working, which reads as "touch works, one thing is broken" and sends you looking
in the wrong file:

| Input | Consumed by |
|---|---|
| `getMoveInput()` — d-pad | `player->input`, ticked with the player |
| `getBuildInput()` — tap to place/dig | `Minecraft::handleBuildAction` |
| `getTurnInput()` — drag to look | `MouseHandler::poll()`, called **only** from `GameRenderer::render` |

`GameRenderer::render` gates the turn block on `mc->mouseGrabbed || mc->useTouchscreen()`.
That `useTouchscreen()` half is load-bearing: `grabMouse()` returns early on a
touch build, so `mouseGrabbed` is false for the whole session and gating on it
alone means the camera cannot turn at all while everything else still works.
That was the 2026-08-12 regression (`acbfa2b`).

Do not "simplify" this by setting `mouseGrabbed = true` on touch. The flag has a
second meaning — `allowGuiClicks = !mouseGrabbed`, because a grabbed cursor is
parked and a dig click would otherwise also hit the hotbar — so that trade gives
you the camera and costs you the hotbar.

## Style

Match what is already here. Commit messages explain the **mechanism** — what was
actually happening and why the fix is the shape it is — and comments in the
platform seams say why a workaround exists, because the surrounding code is from
2013 and the seams are where a 2026 browser disagrees with it. The game's own
simulation, worldgen and rendering code is close to untouched; keep it that way
and fix things in the platform layer where you can.

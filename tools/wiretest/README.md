# wiretest

Three checks on web multiplayer, in increasing order of how much has to be
running and decreasing order of how often you will want to run them.

None of this is wired to CI. The build is the only thing CI does, and these need
a server and a browser; they are here because the parts they cover are the parts
that cannot be checked by reading them.

## `raknet.cpp` — does RakNet work with no threads and no sockets?

The riskiest change in the whole feature. `RakPeer::Startup` no longer spawns
`UpdateNetworkLoop` or a `RecvFromLoop` under `MC_WASM`, the game loop calls
`RunUpdateCycleOnce()` instead, and every datagram goes through a
`SocketLayerOverride`. Two peers, one process, wired to each other the way the
browser wires one peer to the relay.

```sh
source ~/emsdk/emsdk_env.sh
cd handheld/project/web && emcmake cmake -S . -B build && cmake --build build -j8
em++ ../../../tools/wiretest/raknet.cpp \
  -I ../lib_projects/raknet/jni/RaknetSources -DMC_WASM -w -std=c++98 \
  build/libraknet.a -o /tmp/raknet-wiretest.js \
  -sENVIRONMENT=node -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=4194304
node /tmp/raknet-wiretest.js
```

It links the same `libraknet.a` the game does, so the `MC_WASM` branches it
exercises are the ones that ship.

This is the test that found the disconnect bug: `CloseConnection` followed by
four back-to-back `RunUpdateCycleOnce()` calls sends nothing, because the update
cycle decides what to go out from how much time has passed. Four calls in a
microsecond are one frame counted four times. `WebRakNetInstance::disconnect`
spreads them over 40ms of real time for that reason.

## `relay.mjs` — does the switch switch?

Wants only the service.

```sh
node tools/lobby/server.js &
node tools/wiretest/relay.mjs
```

Two sockets, a datagram between them, the 1400-byte case, an unknown
destination, and the announce path — including that a tab cannot publish a route
belonging to somebody else.

## `browser.mjs` — two real tabs

Wants the service, the built page on `:8000`, and `playwright-core` with a
chromium available. Not a dependency of anything else here.

```sh
node tools/lobby/server.js &
(cd handheld/project/web/build && python3 -m http.server 8000) &
npm i playwright-core && node tools/wiretest/browser.mjs
```

Covers the half that only exists in a browser: that the page boots, that
`window.mcpeNet` gets a route, that a datagram put into one tab comes out of the
other, and that the deployment either says multiplayer is on or links to the one
where it is.

## `background.mjs` — does the game keep talking when you look away?

Same wants as `browser.mjs`, plus a display: it needs a **headed** browser, so
`xvfb-run -a` on a box without one. Two Playwright pages are two windows, and an
unfocused window is only throttled — B is opened by A with `window.open` so they
are tabs of one window and switching really backgrounds the other.

```sh
node tools/lobby/server.js &
(cd handheld/project/web/build && python3 -m http.server 8000) &
npm i playwright-core && xvfb-run -a node tools/wiretest/background.mjs
```

The frame loop is `requestAnimationFrame` and the network rides on it, so a tab
in the background stops sending, stops acking and stops draining the relay until
both ends time each other out. This checks that `mcpe_pump_net()` takes over:
that frames collapse, that the pump runs anyway, that an arriving datagram wakes
the tab inside 250ms rather than waiting on the 1s timer, and that the pump
stands down again when frames come back.

It is also where the gate got chosen. `document.hidden` is the obvious signal
and the wrong one: under Xvfb it stays `false` while frames are being throttled
from 25fps to 2, and it would be false in the same way for an occluded or
unfocused window. The test prints it beside the frame counts for that reason.
The pump asks the frame loop whether it is running instead.

It drives `window.mcpeNet` directly and never starts a world, so no peer has
been through `Startup()` and the pump has no RakNet to turn — `raknet.cpp` is
what covers the far side of that call.

## What none of them cover

Driving the game itself — Start Game, Create new, pick a mode, Join Game, click
the row. That path was checked by hand through Playwright while this was built,
end to end: the joiner reaches `onConnect`, the host `onNewClient`, and the
world arrives over `StartGamePacket`/`AddPlayerPacket` with "Steve joined the
game" on screen. It is not scripted here because it drives a 2013 canvas UI by
pixel coordinates, and a test that breaks whenever a menu moves is worse than no
test.

One thing to know before trying anyway: **a headless browser cannot turn the
camera on the desktop build.** Playwright's synthetic mouse moves report
`movementX`/`movementY` of exactly 0 once the canvas holds pointer lock, and
pointer-locked look is the only thing those deltas feed, so the view never
moves however far the cursor is driven. Force the touch build with `?touch=1`
instead: a tap carries its own position, so placing a block needs no aiming at
all, and dragging turns the camera through `TouchTurnInput`, which does not go
through pointer lock. That is how the sign crash of 2026-08-13 was reproduced.

# Minecraft Pocket Edition 0.6.1 — macOS and browser ports

The 2013 MCPE 0.6.1 source, ported to run natively on Apple Silicon macOS and in
a web browser via WebAssembly. Same game that shipped on the iPod touch and
iPad 2, running at 60fps on hardware that did not exist when it was written.

### ▶ Play it: **https://luispl77.github.io/mcpe-0.6.1-macos/**

No install, no plugins. Singleplayer, survival or creative. Worlds save in your
browser and can be downloaded as a `.zip`.

![Title screen](docs/title.png)
![In game](docs/ingame.png)

---

## Why

0.6.1 is my favourite version of my favourite game, and I wanted to actually
play it again rather than read about it. The original build targets are long
dead — Eclipse ADT, `ndk-build`, STLport, a licensing library that no longer
exists — so the game needed new ones.

The encouraging discovery is that the game itself is fine. The simulation,
worldgen, rendering and entity code all compile clean under a 2026 Clang at
`-std=c++03` with barely a change. Everything that needed fixing was in the
platform seams around it.

## What works

| | macOS | Browser |
|---|---|---|
| Worldgen, save, reload | ✅ | ✅ |
| 60 fps | ✅ | ✅ |
| Keyboard + mouse, pointer lock | ✅ | ✅ |
| Sound effects | ✅ | ✅ |
| Fullscreen | ✅ | ✅ |
| Music | ❌ | ❌ |
| Multiplayer | ❌ | ❌ |

**Multiplayer** cannot work in the browser — RakNet needs raw UDP sockets and
browsers have none. It's disabled on both targets rather than half-working.

**Music** was streamed from assets that aren't in the compiled-in PCM sound
bank, so there's nothing to play. Sound effects are all present.

**The options screen is nearly empty.** That's how the leaked source is:
`OptionsScreen::generateOptionScreens()` has every pane except sensitivity
commented out. Not a port bug — and probably the easiest thing to fix first.

## Controls

| | |
|---|---|
| `W` `A` `S` `D` / arrows | Move |
| `Space` | Jump |
| Mouse | Look — **click the game first** to capture the cursor |
| `Esc` | Release the cursor; press again for the pause menu |
| Left click | Dig · Right click | Place |

In the browser the first `Esc` is consumed by the browser itself to exit pointer
lock, which is why it takes two presses to reach the menu.

## Your worlds

Browser worlds live in IndexedDB, which the browser may evict on its own and
which "clear browsing data" erases. **Use the `Download worlds` button** — it
produces a `.zip` that `Restore worlds…` reads back, so you can keep a backup or
move a world to another machine. This has been tested by deleting an entire
browser profile and recovering from the zip.

## Building

**macOS** — needs SDL2, libpng and zlib from Homebrew:

```sh
cd handheld/project/macos
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build -j8
./build/minecraftpe
```

**Browser** — needs [emsdk](https://emscripten.org/docs/getting_started/downloads.html):

```sh
source ~/emsdk/emsdk_env.sh
cd handheld/project/web
emcmake cmake -S . -B build
cmake --build build -j8
cd build && python3 -m http.server 8000   # must be served over http, not file://
```

## The interesting bugs

Nearly every problem came from one root cause: **in this codebase `__APPLE__`
means iOS.** Every desktop and web target keeps falling into mobile-only paths.
That single confusion produced six separate bugs — GL headers, PVRTC textures,
Xperia Play input handling, Android D-pad keycodes overriding WASD, the excluded
PCM sound bank, and the sound backend selection. `grep -rn "__APPLE__" handheld/src`
is the first thing to try when something misbehaves.

The best one was in `anGenBuffers()`:

```c
void anGenBuffers(GLsizei n, GLuint* buffers) {
    static GLuint k = 1;
    for (int i = 0; i < n; ++i) buffers[i] = ++k;   // invented names
}
```

It never called `glGenBuffers`. Desktop GL and GLES 1.1 allow this — binding an
unused buffer name just creates the object. WebGL names are opaque objects that
must come from `createBuffer`, so every draw call in the game failed with
`bufferData: no buffer`. The entire renderer came up from that one fix.

Pointer lock was similar: browsers only grant it inside a user gesture, but the
game grabs the mouse from its frame callback, and `mouseGrabbed` was set anyway
— so the game believed it held a cursor the browser had refused, and never asked
again.

WebGL has no fixed-function pipeline at all, so the renderer runs on
Emscripten's `-sLEGACY_GL_EMULATION` with `GL_FFP_ONLY=1`, which applies cleanly
because the game never binds a shader of its own.

## Layout

```
handheld/src/              the game (~1350 files, C++03)
handheld/project/macos/    macOS CMake target
handheld/project/web/      Emscripten target + HTML shell
handheld/src/main_sdl.h    shared SDL2 entry point, both targets
handheld/src/AppPlatform_sdl.*  shared platform layer
```

The two targets share everything except the text-input dialog (Cocoa `NSAlert`
vs JS `prompt`) and the main loop, where the browser owns the event loop.

## Source

This is the leaked 2013 MCPE 0.6.1 handheld source. It originated from an
Internet Archive upload — **TODO: original archive.org link goes here.**

`main` began as a pristine import of that tree (commit `58e1dea`); every commit
after it is porting work, so `git log` is an exact record of what was changed
and why.

## Legal

Minecraft is a trademark of Mojang AB / Microsoft. This code is theirs, not
mine, and it's published here for preservation and study of a version that has
been unavailable for over a decade. Nothing here is for sale. If a rights holder
objects, the repository comes down.

# AssaultCube Web

[AssaultCube](https://assault.cubers.net/) 1.3 — the free, open-source multiplayer FPS — compiled to
**WebAssembly** and running entirely in the browser: single-player against bots, the map editor and
**online play on the real public servers**.

The original C++ engine is compiled with [Emscripten](https://emscripten.org/). Desktop OpenGL 1.x
runs on WebGL through Emscripten's legacy GL emulation plus a compatibility layer written for this port,
and a small Node.js server bridges the game's UDP networking to WebSockets.

| | |
|---|---|
| Engine | AssaultCube 1.3.0.2 codebase (upstream commit `13f0d8e`), unmodified gameplay |
| Toolchain | Emscripten 7.0.0, WebAssembly + pthreads, WebGL 1 |
| Download | ~2.3 MB code + ~55 MB game data (cached by the browser after the first load) |
| Works | bots (all modes), all official maps, map editor, sound, saved settings, server browser, joining public servers |

---

## Quick start (prebuilt)

The game is already built in [`public/`](public/). You only need [Node.js](https://nodejs.org/) 18 or newer.

```bash
npm install
npm start
```

Then open **http://localhost:8080**. On Windows you can simply double-click **`start.bat`**.

Click the game to capture the mouse (Esc releases it). The first load downloads ~55 MB of game data;
later loads come from the browser cache.

### Why is a server needed?

Two things cannot be done by a plain static file host:

1. **Threads.** The engine uses threads, which in the browser require `SharedArrayBuffer`. Browsers only
   allow that on *cross-origin isolated* pages, i.e. pages served with the
   `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` headers.
   (This is why GitHub Pages cannot host the game as-is.)
2. **Networking.** Browsers cannot open UDP or TCP sockets. [`serve.mjs`](serve.mjs) relays the game's
   sockets (carried over WebSockets) to the real AssaultCube servers.

---

## Repository layout

```
public/                 prebuilt game (what the browser loads)
serve.mjs               local server: static files + network relay + server list
start.bat               Windows launcher
web/
  build.sh              compiles and links the game into public/
  env.sh                puts the repository's Emscripten SDK on PATH
  pre.js                JavaScript that runs before the game (storage, networking, audio, input)
  shell/index.html      the page around the game canvas
  src/webcompat.cpp     OpenGL 1.x -> WebGL compatibility layer
  tools/patch_emscripten.py   two fixes applied to the Emscripten SDK before linking
  tools/check_commands.py     finds script commands with signatures WebAssembly rejects
patches/
  assaultcube-web.patch changes to the AssaultCube C++ source (9 files)
scripts/
  setup.sh              fetches the Emscripten SDK and the AssaultCube source, applies the patch
licenses/               AssaultCube, Cube engine, ENet and asset licenses
```

---

## Building from source

Requirements: `git`, `bash`, Node.js 18+, and Python 3.
On Windows use **Git Bash**; Python is needed once to bootstrap the Emscripten SDK (afterwards the
SDK uses its own bundled Python).

```bash
bash scripts/setup.sh   # once: ~1 GB download (Emscripten SDK + AssaultCube source and assets)
bash web/build.sh       # compile + link into public/ (a few minutes the first time)
npm start
```

`setup.sh` clones the Emscripten SDK into `emsdk/` (pinned to 7.0.0), clones AssaultCube into `ac-src/`,
checks out the commit this port is based on and applies `patches/assaultcube-web.patch`.
Both folders are git-ignored.

`build.sh` options (environment variables): `JOBS=8` parallel compiles, `FORCE=1` recompile
everything, `OPT=-O2` optimisation level, `ASSERTIONS=1` Emscripten runtime checks.

To change engine code, edit files in `ac-src/source/src/`, rebuild, and regenerate the patch with:

```bash
git -C ac-src -c core.autocrlf=false diff > patches/assaultcube-web.patch
```

---

## How the port works

### 1. Compiling the engine

All client sources (game, renderer, bots, the local server used for single-player, and ENet) are
compiled with `em++`. SDL2, SDL2_image, zlib and Ogg/Vorbis come from Emscripten's ports; OpenAL is
provided by Emscripten on top of Web Audio. The game data (`packages/`, `config/`, `bot/`, …) is
packaged into `assaultcube.data` and mounted in an in-memory filesystem.

The build defines `PRODUCTION`: without it AssaultCube identifies itself as a development build
(negative version number, different master server port), which public servers do not accept.

### 2. The main loop

A native game loops forever (`for(;;)`), which would freeze a browser tab. The body of the loop was
moved into `mainloopiteration()` and is driven by `emscripten_set_main_loop`, i.e. by the browser's
`requestAnimationFrame`. The FPS limiter (`SDL_Delay`) is disabled because the browser paces frames.

### 3. OpenGL 1.x on WebGL

AssaultCube renders with the fixed-function OpenGL 1.x pipeline (`glBegin`/`glEnd`, matrix stacks,
texture combiners, fog, client-side vertex arrays). WebGL only has shaders. Emscripten's
`LEGACY_GL_EMULATION` translates the old API into generated shaders, but it covers only part of it and
makes assumptions the engine breaks. [`web/src/webcompat.cpp`](web/src/webcompat.cpp) sits between
the engine and the emulation (the engine's `gl*` calls are redirected to it by macros in `platform.h`):

| Problem | Solution in the compatibility layer |
|---|---|
| The emulation requires every vertex inside `glBegin`/`glEnd` to have the same attributes; the engine sets a colour once, or changes it mid-batch | vertices are buffered with the full current state and replayed with a uniform layout |
| `glDrawArrays(GL_QUADS, first, …)` must start at a multiple of 4, and client arrays are uploaded from vertex 0 | array pointers are rebased so every draw starts at 0 |
| Indexed draws from separate (non-interleaved) arrays copy the wrong vertex range | indexed draws are expanded into one interleaved, non-indexed buffer |
| The world is drawn as ~1100 short strips per frame, and every emulated draw call costs a lot of JavaScript | `glMultiDrawArrays` is implemented by merging all strips of a texture into one triangle list (~1100 → ~110 draws per frame) |
| `glPolygonMode(GL_LINE)` (the editor's wireframe grid) does not exist | polygons are converted to their edges |
| Hundreds of tiny line draws per frame in the editor | untextured lines are batched and drawn in one call; any matrix or state change flushes the batch first |
| Display lists, `glGetTexImage` | stubs (display lists are disabled) |

Other renderer fixes in the patch:

- **Reading pixels.** WebGL can only read back `GL_RGBA`. Model shadow textures were generated by
  reading `GL_RED`, which failed silently and produced garbage shadows (dark blocks under players and
  items); screenshots read `GL_RGB`. Both now read RGBA and convert.
- **Crosshair target.** The engine finds what the crosshair points at by reading the depth buffer,
  which WebGL forbids. It is replaced by a ray march through the cube map (used for aiming and for the
  editor's cursor).
- **Screen size.** The game window always matches the browser window; the size comes from the canvas'
  drawing buffer, because the SDL window size is 0×0 while a page is hidden — which made the
  first-run shadow generation write into a zero-sized buffer and corrupt memory.

### 4. Two fixes to Emscripten itself

[`web/tools/patch_emscripten.py`](web/tools/patch_emscripten.py) patches the SDK before linking
(idempotent):

- **`libglemu.js`** — the regular expression that finds texture lookups in the generated
  fixed-function shaders stops at the first `)`. When a texture matrix is active the lookup is
  `texture2D(u, (u_textureMatrix0 * v_texCoord0).xy)`, the expression is cut in half and the shader
  does not compile. Everything drawn with texture combiners (the whole world) then turned black after a
  few seconds of play.
- **`libsockfs.js`** — every socket's WebSocket URL is tagged with `?proto=udp|tcp`, so the relay knows
  which protocol to use. (Emscripten only announces UDP sockets that were explicitly bound, and the
  game's ping socket is not.)

### 5. Script commands and WebAssembly

AssaultCube calls its console commands through function pointers cast to a generic signature. That
works on x86, but WebAssembly traps on an indirect call whose signature does not match
("function signature mismatch"). [`web/tools/check_commands.py`](web/tools/check_commands.py) scans
all ~500 commands; the two offenders (`setfont` and `setclipboardtext` return a value) are wrapped in
`void` functions.

### 6. Networking

```
 game (wasm) ──socket──> Emscripten ──WebSocket──> serve.mjs ──UDP / TCP──> AssaultCube servers
```

- Each socket peer of the game becomes a WebSocket. [`web/pre.js`](web/pre.js) redirects it to
  `ws://<page host>/relay?to=<host:port>&proto=udp|tcp`, and `serve.mjs` forwards the traffic over a
  real UDP or TCP socket. This carries game traffic (ENet over UDP) and server pings.
- **Server list.** The game asks the master server for the list and then *waits* for the reply. In the
  browser that reply could only be delivered by the very thread that is waiting, so the request comes
  from `serve.mjs` instead (`/master`), via a synchronous HTTP request. The list is cached and refreshed
  in the background, so refreshing it in game takes a few milliseconds.
- The startup ping of package (custom map download) servers is disabled: it blocked the page.

### 7. Saving settings

The profile directory (`/home/web`: settings, key bindings, demos, generated shadow files) is an
IndexedDB-backed filesystem (`IDBFS`). A browser page never "quits", so settings are written every 30 s
and whenever the tab is hidden or closed. *Quit* in the menu saves and shows a "play again" screen.

### 8. Browser integration

- Audio: Chrome starts every `AudioContext` suspended; they are resumed on the first click or key.
- Pointer lock: rejected requests (for example while DevTools has focus) are ignored instead of
  surfacing as errors.

---

## Diagnostics

Built in, useful when something is slow or broken:

- **`http://localhost:8080/?profile`** — samples the JavaScript/WebAssembly call stack while you play
  (JS Self-Profiling API). Press **F8** (or wait 90 s) and a summary of what ran during slow frames
  (> 22 ms) is saved to `profiles/`.
- Main-loop stages that take more than 50 ms are logged to the console as `[slow] <stage> took N ms`.
- From the browser console:
  - `Module.ccall('web_exec', null, ['string'], ['map ac_desert'])` runs any CubeScript command.
  - `Module._web_benchturn(36)` renders 36 frames while turning 360° and logs the time of each frame.

---

## Known limitations

- `serve.mjs` must be running: it is the network bridge. To let other people play without installing
  anything it has to be hosted on a public server — see the security note below.
- Automatic download of custom maps from game servers is untested.
- The first time a map is loaded its sounds and textures are decoded, which causes a short hitch.
- WebGL draws lines 1 pixel wide (editor grid).

### Security note

The relay in `serve.mjs` forwards traffic to any host and port a page asks for (broadcast and multicast
addresses are refused). That is fine on `localhost`. Before exposing it on the internet, restrict the
allowed destinations (for example to the hosts returned by the master server) and add rate limits.

---

## Credits and licenses

AssaultCube is made by the [AssaultCube team](https://assault.cubers.net/) and is based on the
Cube engine by Wouter van Oortmerssen. The engine source is under a zlib-like license; the game's
content (maps, models, textures, sounds) has its own licenses and authors. See [`licenses/`](licenses/)
and the [AssaultCube license page](https://assault.cubers.net/docs/license.html). Some sounds come
from OpenArena and are GPL-licensed.

The porting code in this repository (`web/`, `serve.mjs`, `scripts/`, `patches/`) is provided under the
same zlib-like license as AssaultCube. This is an unofficial port and is not affiliated with the
AssaultCube team.

#!/usr/bin/env bash
# Builds AssaultCube for the browser with Emscripten.
#
#   bash scripts/setup.sh   # once: fetches the Emscripten SDK and the patched AssaultCube source
#   bash web/build.sh       # compiles and links into public/
#
# Environment knobs: JOBS (parallel compiles, default 8), FORCE=1 (recompile everything),
# OPT (default -O2), ASSERTIONS (default 1).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
source "$HERE/env.sh"
AC="$ROOT/ac-src"
SRC="$AC/source/src"
ENET="$AC/source/enet"
OBJ="$ROOT/build/obj"
OUT="$ROOT/public"
[ -d "$SRC" ] || { echo "AssaultCube source not found in $AC - run: bash scripts/setup.sh" >&2; exit 1; }
mkdir -p "$OBJ/bot" "$OBJ/enet" "$OUT"

OPT="${OPT:--O2}"
PORTS=(-sUSE_SDL=2 -sUSE_SDL_IMAGE=2 '-sSDL2_IMAGE_FORMATS=["png","jpg"]' -sUSE_ZLIB=1 -sUSE_VORBIS=1 -sUSE_OGG=1)
COMMON=($OPT -pthread "${PORTS[@]}")
# PRODUCTION: release version number and master server port, required by public servers
CXXFLAGS=("${COMMON[@]}" -DPRODUCTION -fsigned-char -Wno-everything -I"$SRC" -I"$SRC/bot" -I"$ENET/include" -idirafter "$AC/source/include")
CFLAGS=("${COMMON[@]}" -Wno-everything -I"$ENET/include" -DHAS_SOCKLEN_T=1 -DHAS_FCNTL=1 -DHAS_POLL=1 -DHAS_MSGHDR_FLAGS=1 -DHAS_GETADDRINFO=1 -DHAS_GETNAMEINFO=1 -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1)

CLIENT_SRCS="crypto audiomanager autodownload client clientgame clients2c command console docs editing entities http log main menus oggstream openal packetqueue physics protocol rendercubes rendergl renderhud rendermodel renderparticles rendertext rndmap scoreboard serverms server serverbrowser shadow soundlocation soundscheduler stream texture tools water weapon wizard world worldio worldlight worldocull worldrender zip bot/bot bot/botmanager bot/bot_ai bot/bot_util bot/bot_waypoint bot/ac_bot bot/ac_bot_ai"
ENET_SRCS="callbacks compress host list packet peer protocol unix"

compile_one() { # $1 = kind (cxx|c), $2 = name
  local kind="$1" name="$2" src o
  if [ "$kind" = cxx ]; then src="$SRC/$name.cpp"; o="$OBJ/$name.o"; else src="$ENET/$name.c"; o="$OBJ/enet/$name.o"; fi
  if [ -f "$o" ] && [ "$o" -nt "$src" ] && [ -z "${FORCE:-}" ]; then return 0; fi
  local extra=()
  [ "$name" = rendermodel ] && extra=(-fno-strict-aliasing)
  if [ "$kind" = cxx ]; then em++ "${CXXFLAGS[@]}" "${extra[@]}" -c "$src" -o "$o"; else emcc "${CFLAGS[@]}" -c "$src" -o "$o"; fi
  echo "  compiled $name"
}
export -f compile_one
export SRC ENET OBJ
export CXXFLAGS_S="$(printf '%q ' "${CXXFLAGS[@]}")" CFLAGS_S="$(printf '%q ' "${CFLAGS[@]}")"

echo "== compiling"
# build the Emscripten ports (SDL2, vorbis, ...) once, serially, to avoid races in its cache
[ -f "$OBJ/.ports" ] || { em++ "${CXXFLAGS[@]}" -c "$SRC/stream.cpp" -o "$OBJ/stream.o" && touch "$OBJ/.ports"; }
JOBS="${JOBS:-8}"
{ for n in $CLIENT_SRCS; do echo "cxx $n"; done; for n in $ENET_SRCS; do echo "c $n"; done; } | \
  xargs -P "$JOBS" -L 1 bash -c 'eval "CXXFLAGS=($CXXFLAGS_S)"; eval "CFLAGS=($CFLAGS_S)"; compile_one "$0" "$1"'

OBJS=()
for n in $CLIENT_SRCS; do OBJS+=("$OBJ/$n.o"); done
for n in $ENET_SRCS; do OBJS+=("$OBJ/enet/$n.o"); done
em++ "${CXXFLAGS[@]}" -c "$HERE/src/webcompat.cpp" -o "$OBJ/webcompat.o"; OBJS+=("$OBJ/webcompat.o")

echo "== linking"
"$EMSDK_PYTHON" "$HERE/tools/patch_emscripten.py" "$EMSDK"
LDFLAGS=("${COMMON[@]}"
  -lopenal -lidbfs.js
  -sLEGACY_GL_EMULATION=1 -sMAX_WEBGL_VERSION=1
  -sINITIAL_MEMORY=1024MB
  -sSTACK_SIZE=8MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB -sPTHREAD_POOL_SIZE=12
  --profiling-funcs -sGL_UNSAFE_OPTS=0
  -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -sASSERTIONS=${ASSERTIONS:-1}
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,addRunDependency,removeRunDependency,ccall
  -sINVOKE_RUN=1 -sENVIRONMENT=web,worker
  '-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=["$stringToNewUTF8","$UTF8ToString"]'
  --pre-js "$HERE/pre.js"
  --use-preload-cache
  --preload-file "$AC/packages@/packages"
  --preload-file "$AC/config@/config"
  --preload-file "$AC/bot@/bot"
  --preload-file "$AC/mods@/mods"
  --preload-file "$AC/demos@/demos"
  --exclude-file "*.psd" --exclude-file "*.xcf"
)
em++ "${OBJS[@]}" "${LDFLAGS[@]}" -o "$OUT/assaultcube.js"
cp "$HERE/shell/index.html" "$OUT/index.html"
echo "== done: $OUT"
ls -la "$OUT"

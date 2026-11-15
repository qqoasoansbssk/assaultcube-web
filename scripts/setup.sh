#!/usr/bin/env bash
# Prepares everything needed to build from source:
#   ./emsdk   Emscripten SDK, pinned to the version this port was made with
#   ./ac-src  AssaultCube source + assets at the upstream commit the patch is based on,
#             with patches/assaultcube-web.patch applied
# Needs: git, bash, and Python 3 on Linux/macOS (on Windows the SDK brings its own Python).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

EMSDK_VERSION=7.0.0
AC_REPO=https://github.com/assaultcube/AC.git
AC_COMMIT=13f0d8e   # upstream master at the time of the port (AssaultCube 1.3.0.2 codebase)

if [ ! -d emsdk ]; then
  echo "== fetching Emscripten SDK"
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git emsdk
fi
echo "== installing Emscripten $EMSDK_VERSION"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EMSDK_CMD=(./emsdk.bat) ;;
  *) EMSDK_CMD=(./emsdk) ;;
esac
( cd emsdk && "${EMSDK_CMD[@]}" install "$EMSDK_VERSION" && "${EMSDK_CMD[@]}" activate "$EMSDK_VERSION" )

if [ ! -d ac-src ]; then
  echo "== fetching AssaultCube source"
  git -c core.autocrlf=false clone "$AC_REPO" ac-src
  git -C ac-src config core.autocrlf false
fi
cd ac-src
git -c advice.detachedHead=false checkout "$AC_COMMIT"
if git -c core.autocrlf=false apply --check --whitespace=nowarn "$ROOT/patches/assaultcube-web.patch" 2>/dev/null; then
  echo "== applying patches/assaultcube-web.patch"
  git -c core.autocrlf=false apply --whitespace=nowarn "$ROOT/patches/assaultcube-web.patch"
elif git -c core.autocrlf=false apply --reverse --check --whitespace=nowarn "$ROOT/patches/assaultcube-web.patch" 2>/dev/null; then
  echo "== patch already applied"
else
  echo "patches/assaultcube-web.patch does not apply cleanly to ac-src" >&2
  exit 1
fi

cd "$ROOT"
[ -d node_modules/ws ] || { echo "== installing server dependency"; npm install --no-audit --no-fund; }
echo
echo "Ready. Build with:  bash web/build.sh"

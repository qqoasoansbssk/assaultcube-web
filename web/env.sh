# Source this to put the repository's Emscripten SDK (./emsdk) on PATH.
# Works in Git Bash on Windows and in bash on Linux/macOS.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export EMSDK="$ROOT/emsdk"
[ -d "$EMSDK" ] || { echo "Emscripten SDK not found in $EMSDK - run: bash scripts/setup.sh" >&2; return 1 2>/dev/null || exit 1; }
export EM_CONFIG="$EMSDK/.emscripten"

# Python and Node bundled with the SDK (Windows) or from the system (Linux/macOS)
py="$(ls -d "$EMSDK"/python/*/python.exe 2>/dev/null | head -1)"
nodedir="$(ls -d "$EMSDK"/node/*/ 2>/dev/null | head -1)"
export EMSDK_PYTHON="${py:-$(command -v python3 || command -v python)}"
[ -n "$nodedir" ] && export PATH="${nodedir%/}:${nodedir%/}/bin:$PATH"
export PATH="$EMSDK/upstream/emscripten:$EMSDK/upstream/bin:$PATH"
unset py nodedir

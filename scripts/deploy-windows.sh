#!/usr/bin/env bash
# Collect Qt + MinGW runtime DLLs next to Waltosh.exe for a portable Windows build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_EXE="${1:-$ROOT/build/Waltosh.exe}"
DEST="${2:-$ROOT/dist/windows}"
PREFIX="${MINGW_PREFIX:-/ucrt64}"

if [[ ! -f "$SRC_EXE" ]]; then
  echo "error: missing executable: $SRC_EXE" >&2
  exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST"
cp "$SRC_EXE" "$DEST/Waltosh.exe"

# secp256k1 shared library (Windows)
if [[ -f "$ROOT/build/ltcengine/_deps/secp256k1-build/src/libsecp256k1-2.dll" ]]; then
  cp "$ROOT/build/ltcengine/_deps/secp256k1-build/src/libsecp256k1-2.dll" "$DEST/" || true
fi
# Also pick up DLL if copied next to the exe by POST_BUILD
shopt -s nullglob
for dll in "$(dirname "$SRC_EXE")"/*secp256k1*.dll; do
  cp "$dll" "$DEST/"
done

if command -v windeployqt6 >/dev/null 2>&1; then
  windeployqt6 \
    --release \
    --no-translations \
    --compiler-runtime \
    "$DEST/Waltosh.exe"
elif command -v windeployqt >/dev/null 2>&1; then
  windeployqt \
    --release \
    --no-translations \
    --compiler-runtime \
    "$DEST/Waltosh.exe"
else
  echo "warning: windeployqt not found; copying dependencies via ldd only" >&2
fi

copy_deps_for() {
  local binary="$1"
  local dll
  while IFS= read -r dll; do
    [[ -n "$dll" ]] || continue
    [[ -f "$dll" ]] || continue
    case "$dll" in
      "${PREFIX}/bin/"*)
        local base
        base="$(basename "$dll")"
        if [[ ! -f "$DEST/$base" ]]; then
          cp "$dll" "$DEST/$base"
          echo "copied $base"
        fi
        ;;
    esac
  done < <(ldd "$binary" 2>/dev/null | awk '/=>/ {print $3}')
}

# Repeat until no new DLLs appear (transitive MinGW deps: zlib, zstd, pcre2, ...).
for _ in $(seq 1 12); do
  before="$(find "$DEST" -type f | wc -l)"
  while IFS= read -r -d '' file; do
    copy_deps_for "$file"
  done < <(find "$DEST" -type f \( -name '*.exe' -o -name '*.dll' \) -print0)
  after="$(find "$DEST" -type f | wc -l)"
  [[ "$before" -eq "$after" ]] && break
done

# Ensure core MinGW runtime is present even if ldd naming differs.
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll zlib1.dll; do
  if [[ -f "${PREFIX}/bin/${dll}" && ! -f "${DEST}/${dll}" ]]; then
    cp "${PREFIX}/bin/${dll}" "$DEST/"
    echo "copied $dll"
  fi
done

echo "Deployed to $DEST"
find "$DEST" -type f | sort

#!/bin/sh
# Build the loose-asset tree the browser build downloads. The browser has no paks, so
# every file the engine opens at runtime has to be present. MAPS selects the maps.
#
#   MAPS="q3dm17 q3dm6" ./tools/build-asset-dir.sh [dest]
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
MAPS=${MAPS:-q3dm17}
DEST=${1:-$HERE/assets-wasm/baseq3}

rm -rf "$DEST"
mkdir -p "$DEST"

# pak8 is the 1.32 shareware pak: the VMs, the arena models, levels and their textures.
# pak2 fills out the rest of the game (HUD, sound, items, cgame/ui qvm). Nothing is
# overwritten, so pak8 keeps priority.
for pak in 8 2; do
	pk3="$HERE/baseq3/pak$pak.pk3"
	[ -f "$pk3" ] && unzip -o -q -n "$pk3" -d "$DEST"
done

# default.cfg is read at startup and productid.txt is what keeps the engine out of
# restricted demo mode (FS_SetRestrictions restarts the filesystem into DEMOGAME without
# it, and the baseq3 assets then never enter the search path). Both are loose in pak0.
unzip -o -q -n "$HERE/baseq3/pak0.pk3" -d "$DEST" "default.cfg" "productid.txt" 2>/dev/null || true

# pak0 holds the maps and the texture/shader set. The closure is computed statically from
# the BSP shader lump, the shader scripts, the md3 surface names, the map's own files, and
# the shader and model paths the cgame/UI name in C string literals.
python3 "$HERE/tools/map-shaders.py" --extract "$DEST" "$HERE/baseq3" $MAPS

echo "assets: $(find "$DEST" -type f | wc -l | tr -d ' ') files, $(du -sh "$DEST" | cut -f1) -> $DEST"

#!/bin/bash

set -e

cd "$(dirname "$0")"

if ! test -f build-native/Quake3 >/dev/null 2>&1; then
	echo "error: build-native/Quake3 doesn't exist; have you run build-native.sh?"
	exit 1
fi

if ! test -f baseq3/pak0.pk3 >/dev/null 2>&1; then
	echo "error: baseq3/pak0.pk3 doesn't exist; have you put your Quake III resources in baseq3?"
	exit 1
fi

# SDL_WINDOW_DISPLAY takes a display index or a name substring. Unset, the engine
# picks the built-in panel ("Color LCD" / "Built-in Retina Display") so the window
# lands on the laptop screen. SDL_WINDOW_XY=<x>,<y> pins a position.

./build-native/Quake3 \
	+set r_fullscreen "${FULLSCREEN:-0}" \
	+set r_mode -1 +set r_customwidth "${WIDTH:-1280}" +set r_customheight "${HEIGHT:-720}" \
	+set logfile "${LOGFILE:-2}" +set developer "${DEVELOPER:-1}" \
	${@}

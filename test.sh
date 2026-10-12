#!/bin/bash

set -e

cd "$(dirname "$0")"

if [[ "${SKIP_BUILD}" != "1" ]]; then
	DEBUG=1 ./build-native.sh
	echo ''
fi

echo "== display pick =="
cc -o /tmp/display-pick-test tools/display-pick-test.c
/tmp/display-pick-test

echo
echo "== SE_CHAR / SE_KEY pump =="
rm -f /tmp/selftest.log
./build-native/Quake3 +set r_fullscreen 0 +set r_mode -1 \
	+set r_customwidth 640 +set r_customheight 480 \
	+set in_selftest 1 +devmap q3dm17 > /tmp/selftest.log 2>&1 &
pid=$!
sleep 8
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

grep -q 'SELFTEST: console field "a"' /tmp/selftest.log \
	&& echo "ok: SDL_TEXTINPUT -> SE_CHAR, BackSpace -> SE_CHAR 8, console field edits" \
	|| { echo "FAIL: see /tmp/selftest.log"; exit 1; }

echo
echo "== multiplayer loopback =="
python3 tools/mptest.py

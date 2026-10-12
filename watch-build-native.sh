#!/bin/bash

set -e

if ! command -v entr >/dev/null 2>&1; then
	echo "error: entr not found; have you installed it?"
	exit 1
fi

cd "$(dirname "$0")"

bash -c "find CMakeLists.txt ; find code -type f -name '*.c'; find code -type f -name '*.h'" \
	| grep -v 'build-' | entr -n -r -cc -s "DEBUG=${DEBUG} ./build-native.sh"

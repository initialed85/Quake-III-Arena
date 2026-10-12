#!/bin/bash

set -e

if ! command -v git >/dev/null 2>&1; then
	echo "error: git not found; have you installed it?"
	exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
	echo "error: cmake not found; have you installed it?"
	exit 1
fi

if ! command -v make >/dev/null 2>&1; then
	echo "error: make not found; have you installed it?"
	exit 1
fi

pushd "$(dirname "$0")" >/dev/null

if [[ "${CLEAN}" == "1" ]]; then
	rm -fr build-native 2>&1 || true
fi

mkdir -p build-native
cd build-native

if command -v clang-20 >/dev/null 2>&1; then
	CC="$(which clang-20)"
else
	CC="$(which clang)"
fi

if command -v clang++-20 >/dev/null 2>&1; then
	CXX="$(which clang++-20)"
else
	CXX="$(which clang++)"
fi

export CC
export CXX

if [[ "${DEBUG}" == "1" ]]; then
	cmake -Wno-dev -DCMAKE_BUILD_TYPE=Debug -B . -S ../
else
	cmake -Wno-dev -DCMAKE_BUILD_TYPE=Release -B . -S ../
fi

make

echo ""

find ./ -maxdepth 1 -type f -name 'Quake3' || true
find ./ -maxdepth 1 -type f -name '*.dylib' || true
find ./ -maxdepth 1 -type f -name '*.so' || true

popd >/dev/null

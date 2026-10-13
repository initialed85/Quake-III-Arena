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

# emsdk is pinned so the build is reproducible; it is gitignored
if ! test -e emsdk >/dev/null 2>&1; then
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
    git checkout tags/4.0.20
    ./emsdk install 4.0.20
    cd ..
fi

cd emsdk
./emsdk activate 4.0.20
# shellcheck disable=SC1091
source ./emsdk_env.sh
cd ..

if [[ "${CLEAN}" == "1" ]]; then
    rm -fr build-wasm 2>&1 || true
fi

mkdir -p build-wasm
pushd build-wasm >/dev/null

if [[ "${DEBUG}" == "1" ]]; then
    emcmake cmake -Wno-dev -DCMAKE_BUILD_TYPE=Debug -B . -S ../
else
    emcmake cmake -Wno-dev -DCMAKE_BUILD_TYPE=Release -B . -S ../
fi

emmake make

echo ""

find ./ -maxdepth 1 -type f -name '*.js' || true
find ./ -maxdepth 1 -type f -name '*.html' || true
find ./ -maxdepth 1 -type f -name '*.wasm' || true
find ./ -maxdepth 1 -type f -name '*.data' || true

popd >/dev/null

#!/bin/bash -e

rm -rf build
mkdir -p build
pushd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
MTL_HUD_ENABLED=1 ./main

popd

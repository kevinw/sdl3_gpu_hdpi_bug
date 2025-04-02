#!/bin/bash -e

# rm -rf build
mkdir -p build
pushd build

cmake ..
cmake --build .
./main

popd

#! /bin/bash
rm -rf build/ \
    && cmake -S . -B build -DGOC_ENABLE_STATS=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

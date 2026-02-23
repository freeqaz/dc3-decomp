#!/bin/bash
# Configure build with local tool paths
cd "$(dirname "$0")/../.." || exit 1
python3 configure.py \
    --dtk ../jeff/target/release/dtk \
    --objdiff ../objdiff/target/release/objdiff-cli \
    --wibo ../wibo/build/release/wibo

#!/bin/bash
# Writes src/frame_xr/input/kiu_hooks_stubs.h from stubs.s and stubs-eur.inc.
set -e
cd "$(dirname "$0")"
python3 -I gen.py > ../../src/frame_xr/input/kiu_hooks_stubs.h.new
mv ../../src/frame_xr/input/kiu_hooks_stubs.h.new ../../src/frame_xr/input/kiu_hooks_stubs.h
echo "wrote src/frame_xr/input/kiu_hooks_stubs.h"

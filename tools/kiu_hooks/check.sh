#!/bin/bash
# The stubs' desktop check (not part of the build): 1. stubs.s assembles to exactly the words in
# src/frame_xr/input/kiu_hooks_stubs.h; 2. the same instructions, with the literals pointing at a
# test bench (harness.c, harness_asm.s), do what they say under qemu-arm: the maths, every
# register, sp and lr kept, each guard. Needs clang, ld.lld, llvm-mc and qemu-arm(-static).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -I gen.py > "$T/stubs.h"
cmp "$T/stubs.h" ../../src/frame_xr/input/kiu_hooks_stubs.h && echo "kiu_hooks_stubs.h matches stubs.s"
CF="--target=armv6k-unknown-linux-gnueabihf -mfpu=vfp -mfloat-abi=hard -marm"
( echo '.equ IMAGE_VERSION, 4'; cat stubs.s ) > "$T/stubs-test.s"
clang $CF -c -o "$T/stubs.o" "$T/stubs-test.s"
clang $CF -c -o "$T/hasm.o" harness_asm.s
clang $CF -O1 -ffreestanding -fno-builtin -fno-stack-protector -c -o "$T/h.o" harness.c
ld.lld -static -e _start -o "$T/harness" "$T/h.o" "$T/hasm.o" "$T/stubs.o"
Q=$(command -v qemu-arm-static || command -v qemu-arm)
cd "$T" # a crash's core file lands in the temporary folder, never in the tree
"$Q" "$T/harness"

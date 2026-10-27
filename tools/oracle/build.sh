#!/bin/sh
# Cross-compiles the oracle test harness (needs mingw-w64 i686) and runs it under Wine.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${OUT:-$root/build-oracle}
mkdir -p "$out"
i686-w64-mingw32-gcc -O2 -c "$here/rdimage.c" -o "$out/rdimage.o"
i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -ffp-contract=off \
    -I"$root/core/include" -I"$here" \
    "$here/oracle_main.cpp" $(ls "$root"/core/src/*.cpp) "$out/rdimage.o" \
    -static -Wl,--image-base,0x00200000 -Wl,--section-start=.rdimg=0x00400000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section -s -o "$out/oracle.exe"
echo "built $out/oracle.exe"

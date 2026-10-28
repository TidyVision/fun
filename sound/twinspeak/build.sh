#!/usr/bin/env bash
# twinspeak v6.5.1 build -- llvm-mingw when present, pip zig otherwise.
#
# v6.5.1 keeps the v6.5 optimization-pass flags untouched (display-only
# change):
#   -O3                 whole-program-grade optimization of the bulk paths
#   -march=x86-64-v3    SSE4.2/AVX2 auto-vectorization (M720q = Coffee Lake;
#                       any 2013+ x86-64 CPU runs the binary)
#   -ffp-contract=off   no mul+add may fuse into an FMA -- every rounding is
#                       exactly the v6.4 scalar one (the bit-exact guarantee)
#   -flto               whole-program inlining of mixBlock/ring ops
set -e
cd "$(dirname "$0")"

# v6.5.5: resources (icon + VERSIONINFO -> Task Manager name "TwinSpeak").
# zig rc emits a .res that zig c++ links directly; windres emits a COFF
# object via -O coff that either toolchain links. windows.h not needed.
RESOBJ=""
if command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then
    echo "[build] resources: system windres"
    x86_64-w64-mingw32-windres -O coff -F pe-x86-64 twinspeak.rc -o twinspeak_res.obj
    RESOBJ="twinspeak_res.obj"
elif ls -d /home/z/my-project/work/toolchain/llvm-mingw*/bin >/dev/null 2>&1; then
    TC="$(ls -d /home/z/my-project/work/toolchain/llvm-mingw*/bin | head -1)"
    echo "[build] resources: llvm-mingw windres"
    "${TC}/x86_64-w64-mingw32-windres" -O coff twinspeak.rc -o twinspeak_res.obj
    RESOBJ="twinspeak_res.obj"
elif [ -x /home/z/my-project/venv-zig/bin/python ]; then
    echo "[build] resources: zig rc"
    /home/z/my-project/venv-zig/bin/python -m ziglang rc twinspeak.rc
    RESOBJ="twinspeak.res"
else
    echo "[build] FATAL: no resource compiler (windres / llvm-mingw / venv-zig)" >&2
    exit 1
fi

CXXFLAGS="-O3 -ffp-contract=off -std=c++17 -Wall -Wextra -Wno-unused-parameter -static"
# march spelling differs by driver: clang (llvm-mingw) takes x86-64-v3,
# zig cc takes its native CPU name x86_64_v3 -- same ISA level (AVX2).

if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo "[build] llvm-mingw"
    x86_64-w64-mingw32-g++ $CXXFLAGS -march=x86-64-v3 -flto \
        -o twinspeak.exe twinspeak.cpp "$RESOBJ" \
        -lole32 -lwinmm -luuid -ladvapi32 -luser32 -lkernel32 -lsetupapi \
        -Wl,--subsystem,console
elif ls -d /home/z/my-project/work/toolchain/llvm-mingw*/bin >/dev/null 2>&1; then
    TC="$(ls -d /home/z/my-project/work/toolchain/llvm-mingw*/bin | head -1)"
    echo "[build] llvm-mingw at $TC"
    "${TC}/x86_64-w64-mingw32-g++" $CXXFLAGS -march=x86-64-v3 -flto \
        -o twinspeak.exe twinspeak.cpp "$RESOBJ" \
        -lole32 -lwinmm -luuid -ladvapi32 -luser32 -lkernel32 -lsetupapi \
        -Wl,--subsystem,console
else
    echo "[build] zig (pip)"
    # NOTE: no -flto on the zig path -- zig 0.17 + lld-link + static mingw-w
    # CRT leaves the long-double math stubs (lrintl, frexpl, ...) undefined
    # under LTO. -O3 + x86_64_v3 carries the pass; llvm-mingw keeps LTO.
    /home/z/my-project/venv-zig/bin/python -m ziglang c++ $CXXFLAGS -march=x86_64_v3 \
        -target x86_64-windows-gnu \
        -o twinspeak.exe twinspeak.cpp "$RESOBJ" \
        -lole32 -lwinmm -luuid -ladvapi32 -luser32 -lkernel32 -lsetupapi \
        -Wl,/subsystem:console
fi
echo "[build] OK -> twinspeak.exe"

#!/bin/sh
# AddressSanitizer harness for the frame test. NOT one of the six gates in tools/check.sh:
# the project build uses VS's clang-cl toolset, which ships no ASan runtime, so this script
# builds the same sources with the standalone LLVM clang (C:/Program Files/LLVM/bin) instead.
#
#     sh tools/asan.sh                 # tests/test_geoscapeframe.c, output in build/asan
#     sh tools/asan.sh test_geoscape   # any other single-file test in tests/
#
# It builds nothing in build/ that the gates use and changes no source. Read the reports in
# build/asan/run_stderr.log; ASAN_OPTIONS keeps the run going after the first report so one
# run finds every site, and leaks are off because the shell leaves the UI singletons reachable
# on purpose (the original's dangling *g_pUiCloseCtl at 0x186B00 is reproduced as written).
#
# Measured at Part 118: one report in the whole frame run -- heap-use-after-free, a WRITE of
# 8 bytes in BaseFacScreen_Init (src/pufo/game/pufo_baseui.c:7205) into a 56-byte block freed
# by GeoscapeUi_FreeAll (src/pufo/ui/pufo_geouimanage.c:121) through the freed close control;
# the IDB says the original frees that control at 0x86F68 and never nulls the global, so the
# store through it is faithful. No global-buffer-overflow on the product columns in either
# the 4096-entry layout or the 1600-entry layout (PLAN gap 118c).
set -e
cd "$(git rev-parse --show-toplevel)"

TEST="${1:-test_geoscapeframe}"
CC="${CLANG_ASAN:-/c/Program Files/LLVM/bin/clang.exe}"
OUT=build/asan
OBJ="$OUT/obj"
ASAN_LIB_DIR=$(ls -d "/c/Program Files/LLVM/lib/clang"/*/lib/windows | tail -1)

rm -rf "$OUT"
mkdir -p "$OBJ"
cp "$ASAN_LIB_DIR/clang_rt.asan_dynamic-x86_64.dll" "$OUT/"

FLAGS="-std=c23 -O1 -g -fsanitize=address -fno-omit-frame-pointer -Isrc/pufo -Isrc/pufo/app -Isrc/pufo/ui"

if [ ! -f "tests/$TEST.c" ]; then
    echo "no tests/$TEST.c"
    exit 1
fi

n=0
for f in $(find src/pufo -name '*.c' ! -name pufo_app.c ! -name pufo_main.c | sort); do
    "$CC" $FLAGS -c "$f" -o "$OBJ/$(echo "$f" | tr '/' '_' | tr '.' '_').obj" 2>> "$OUT/compile.log"
    n=$((n+1))
done
"$CC" $FLAGS -c "tests/$TEST.c" -o "$OBJ/$TEST.obj" 2>> "$OUT/compile.log"
"$CC" -fsanitize=address "$OBJ"/*.obj -o "$OUT/tgf.exe" \
      -luser32 -lgdi32 -lwinmm -ladvapi32 >> "$OUT/compile.log" 2>&1
echo "built $n library files + tests/$TEST.c into $OUT"

ASAN_OPTIONS=detect_leaks=0:abort_on_error=0:halt_on_error=0:print_stacktrace=1 \
    "$OUT/tgf.exe" > "$OUT/run_stdout.log" 2> "$OUT/run_stderr.log" || true

r=$(grep -c "ERROR: AddressSanitizer" "$OUT/run_stderr.log" || true)
echo "ASan reports: $r  (full text: $OUT/run_stderr.log)"
grep -A8 "ERROR: AddressSanitizer" "$OUT/run_stderr.log" | head -60

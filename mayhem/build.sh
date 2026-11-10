#!/usr/bin/env bash
#
# mayhem/build.sh — build libiec61850 (sanitized) + the mining harness mayhem/harness/*.c,
# plus the normal-flags library and the functional self-test that mayhem/test.sh runs.
# Runs in the commit image as `mayhem` in /mayhem; re-runnable and offline (no fetches: pure CMake + clang).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
# Upstream stores destructors as `void (*)(void*)` through casts (linked_list.c:71), which UBSan's
# `function` check flags on every GooseSubscriber/SVSubscriber/IsoServer teardown. Relax only that check.
SANITIZER_FLAGS="$SANITIZER_FLAGS -fno-sanitize=function"
# SanCov instrumentation for the library and harness objects (unconditional, also with empty SANITIZER_FLAGS):
# $LIB_FUZZING_ENGINE only instruments the TU it links, so without this the fuzzed code reports 0 edges.
COV_FLAGS="-fsanitize=fuzzer-no-link"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# 1) The project itself, instrumented. build/ is gitignored upstream, so `git clean -ffdX` removes it.
cmake -S . -B build/fuzz -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
      -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF \
      -DCMAKE_C_FLAGS="-O1 $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS" \
      -DCMAKE_CXX_FLAGS="-O1 $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS"
cmake --build build/fuzz -j"$MAYHEM_JOBS"

# Header include dirs (exclude src/vs, whose stdbool.h shim shadows the system one).
INC=$(find src hal -name '*.h' -not -path '*/vs/*' -printf '-I%h\n' | sort -u | tr '\n' ' ')
INC="$INC -Ibuild/fuzz/config -Iconfig"
LIBS="build/fuzz/src/libiec61850.a build/fuzz/hal/libhal.a -lpthread -lm"

# 2) Harnesses: fuzzer binary + standalone reproducer; LeakSanitizer off (SPEC §6.2 item 15).
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.c -o build/fuzz/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/fuzz/standalone_main.o
for src in mayhem/harness/*.c; do
  name=$(basename "$src" .c)
  $CC -O1 $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS -include stdbool.h $INC -c "$src" -o "build/fuzz/$name.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "build/fuzz/$name.o" build/fuzz/lsan_off.o $LIBS -o "$SRC/$name"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS build/fuzz/standalone_main.o "build/fuzz/$name.o" build/fuzz/lsan_off.o $LIBS -o "$SRC/$name-standalone"
done

# 3) The functional self-test, against a separate library build with the project's NORMAL flags.
cmake -S . -B build/normal -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
      -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF \
      -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS"
cmake --build build/normal -j"$MAYHEM_JOBS"
NINC=$(find src hal -name '*.h' -not -path '*/vs/*' -printf '-I%h\n' | sort -u | tr '\n' ' ')
$CC -O2 $COVERAGE_FLAGS -include stdbool.h $NINC -Ibuild/normal/config -Iconfig mayhem/selftest.c \
    build/normal/src/libiec61850.a build/normal/hal/libhal.a -lpthread -lm -o build/selftest

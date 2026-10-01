#!/usr/bin/env bash
# Verifies, with a real cross-compile and link, the two size claims made for FloatText
# (src/utils/FloatText.h, tracking issue embedded-libraries/libArT-proto#28):
#
#   1. A program that never calls utils::FloatText::formatFloat()/parseFloat() links none of
#      FloatText's or Ryu's code.
#   2. A program that does call them does not pull in newlib's assert()/printf()/malloc() chain
#      (see the NDEBUG guard at the top of src/utils/ryu_float.c).
#
# For (1), FloatText.o and ryu_float.o are linked directly (not archived into a .a first): an
# archive member is only ever pulled in for an outstanding undefined symbol, so a program that
# references nothing from it would trivially "pass" whether or not FloatText's own code is
# actually dead-strippable. Linking the objects directly and relying on --gc-sections (with
# -ffunction-sections/-fdata-sections) to drop what is unreferenced is what actually exercises the
# claim - for example, it is the only way this catches an accidental static constructor (which
# --gc-sections keeps as a GC root regardless of whether anything calls into it).
#
# CXX_STANDARD selects the language standard of the C++ files (default 23; CI runs 17 and 23). The
# compat headers then use the own art::expected/etl::span below C++23 and the std types at C++23,
# so below C++23 the ETL include directory is needed as well (ETL_INCLUDE_DIR, e.g.
# <build>/_deps/etl-src/include); a missing one is handled like a missing Ryu tree below.
#
# Needs a cross C/C++ compiler pair (default: arm-none-eabi-gcc/g++, override with CROSS_CC/
# CROSS_CXX/CROSS_NM) and the populated Ryu source tree (RYU_SOURCE_DIR, e.g.
# <build>/_deps/ryu-src after configuring this project once). By default, skips gracefully
# (exit 0) when either is missing, so this is safe to run outside a full firmware toolchain; set
# REQUIRE_TOOLCHAIN=1 (CI does) to turn that into a hard failure instead - a skip that still exits
# 0 is indistinguishable from a pass in a pipeline's job list.
set -euo pipefail

CC="${CROSS_CC:-arm-none-eabi-gcc}"
CXX="${CROSS_CXX:-arm-none-eabi-g++}"
NM="${CROSS_NM:-arm-none-eabi-nm}"

missing=""
if ! command -v "$CC" >/dev/null 2>&1; then missing="$missing $CC"; fi
if ! command -v "$CXX" >/dev/null 2>&1; then missing="$missing $CXX"; fi
if ! command -v "$NM" >/dev/null 2>&1; then missing="$missing $NM"; fi
if [ -n "$missing" ]; then
   echo "Cross toolchain tool(s) not found on PATH:$missing"
   if [ "${REQUIRE_TOOLCHAIN:-0}" = "1" ]; then
      echo "REQUIRE_TOOLCHAIN=1: treating this as a failure instead of skipping." >&2
      exit 1
   fi
   echo "Skipping the FloatText zero-cost check (set REQUIRE_TOOLCHAIN=1 to make this a failure)."
   exit 0
fi

if [ -z "${RYU_SOURCE_DIR:-}" ] || [ ! -d "$RYU_SOURCE_DIR" ]; then
   echo "RYU_SOURCE_DIR is not set to a populated Ryu checkout."
   echo "(configure this project with CMake at least once so FetchContent populates it, then pass"
   echo " RYU_SOURCE_DIR=<build dir>/_deps/ryu-src)"
   if [ "${REQUIRE_TOOLCHAIN:-0}" = "1" ]; then
      echo "REQUIRE_TOOLCHAIN=1: treating this as a failure instead of skipping." >&2
      exit 1
   fi
   echo "Skipping the FloatText zero-cost check (set REQUIRE_TOOLCHAIN=1 to make this a failure)."
   exit 0
fi

ETL_FLAGS=""
if [ "${CXX_STANDARD:-23}" -lt 23 ]; then
   if [ -z "${ETL_INCLUDE_DIR:-}" ] || [ ! -f "$ETL_INCLUDE_DIR/etl/span.h" ]; then
      echo "ETL_INCLUDE_DIR is not set to the include directory of an ETL checkout, which C++${CXX_STANDARD} needs."
      if [ "${REQUIRE_TOOLCHAIN:-0}" = "1" ]; then
         echo "REQUIRE_TOOLCHAIN=1: treating this as a failure instead of skipping." >&2
         exit 1
      fi
      echo "Skipping the FloatText zero-cost check (set REQUIRE_TOOLCHAIN=1 to make this a failure)."
      exit 0
   fi
   ETL_FLAGS="-isystem $ETL_INCLUDE_DIR"
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Cortex-M4F: one of the two reference cores FloatText's flash size is measured against (the
# other, Cortex-M0+, exercises the soft-division/soft-multiply paths instead; the property this
# script checks - present iff called, absent iff not - does not depend on which core is used).
MFLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
CXXFLAGS="-Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables -std=c++${CXX_STANDARD:-23} $MFLAGS -I$SRC -I$RYU_SOURCE_DIR $ETL_FLAGS"
CFLAGS="-Os -ffunction-sections -fdata-sections $MFLAGS -I$RYU_SOURCE_DIR"

# Deliberately no -DNDEBUG on the command line: ryu_float.c must keep newlib's assert/printf/
# malloc chain out on its own, whatever the containing build's own build type is (ESP-IDF and a
# CMAKE_BUILD_TYPE=Debug build both leave NDEBUG unset).
"$CXX" -c $CXXFLAGS -o "$WORK/FloatText.o" "$SRC/utils/FloatText.cpp"
"$CC" -c $CFLAGS -o "$WORK/ryu_float.o" "$SRC/utils/ryu_float.c"

cat >"$WORK/unused.cpp" <<'SRC_EOF'
// Calls nothing from FloatText or Ryu.
extern "C" void _start() {
   for(;;) {
   }
}
SRC_EOF

cat >"$WORK/used.cpp" <<'SRC_EOF'
#include <utils/FloatText.h>
volatile float g_value = 0.0f;
char g_buf[32];
extern "C" void _start() {
   const size_t len = utils::FloatText::formatFloat(g_value, g_buf);
   auto parsed = utils::FloatText::parseFloat(std::string_view(g_buf, len));
   if(parsed) {
      g_value = *parsed;
   }
   for(;;) {
   }
}
SRC_EOF

echo "== C++${CXX_STANDARD:-23} =="
status=0
for variant in unused used; do
   "$CXX" -c $CXXFLAGS -o "$WORK/$variant.o" "$WORK/$variant.cpp"
   # FloatText.o and ryu_float.o are always on this link line, for both variants: it is
   # --gc-sections, not the linker's archive-member selection, that must drop them when unused.
   "$CXX" -nostartfiles --specs=nano.specs --specs=nosys.specs $MFLAGS \
      -Wl,--gc-sections -Wl,-e,_start -Wl,--build-id=none \
      -o "$WORK/$variant.elf" "$WORK/$variant.o" "$WORK/FloatText.o" "$WORK/ryu_float.o"
done

echo "== unused.elf must contain no FloatText/Ryu code =="
if "$NM" "$WORK/unused.elf" | grep -Ei "f2d|f2s|s2f_n|floattext|art_ryu_f2d|float_pow5"; then
   echo "FAIL: FloatText/Ryu code was linked into a program that never calls it." >&2
   status=1
else
   echo "  none found - OK"
fi

echo "== used.elf must actually contain FloatText/Ryu code =="
if ! "$NM" "$WORK/used.elf" | grep -Eiq "floattext|art_ryu_f2d|s2f_n"; then
   echo "FAIL: FloatText was called but its symbols are missing from the link." >&2
   status=1
else
   echo "  found - OK"
fi

echo "== used.elf must not pull in newlib's assert/printf/malloc chain =="
if "$NM" "$WORK/used.elf" | grep -Ei "assert|printf|malloc|_impure"; then
   echo "FAIL: FloatText pulled in newlib's assert/printf/malloc chain (NDEBUG guard broken?)." >&2
   status=1
else
   echo "  none found - OK"
fi

exit "$status"

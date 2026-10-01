#!/usr/bin/env bash
# Compiles the compat headers (src/utils/compat/) like firmware: with arm-none-eabi-g++, -fno-exceptions and -fno-rtti,
# through tests/firmware/CompatFirmwareBuild.cpp, in three configurations:
#
#   - C++17 with the own types (ART_USE_STD_EXPECTED_SPAN=0), the configuration of a C++17 firmware;
#   - C++23 with the own types, a C++23 libArT linked into a C++17 application;
#   - C++23 with std::expected and std::span (ART_USE_STD_EXPECTED_SPAN=1), the default.
#
# The native unit tests build with exceptions and RTTI on, so they would not notice a throw, a typeid or a
# dynamic_cast in the compat headers. Here these fail the build, and the object file must not reference the
# exception or RTTI runtime (__cxa_throw, __cxa_begin_catch, std::__throw_out_of_range_fmt(), _Unwind_*,
# __aeabi_unwind_cpp_pr*, the type_info vtables, ...).
#
# Needs arm-none-eabi-g++ and arm-none-eabi-nm (override with CROSS_CXX/CROSS_NM) and the ETL include directory
# (ETL_INCLUDE_DIR, e.g. <build>/_deps/etl-src/include after configuring this project once). Like
# check-floattext-zero-cost.sh, it skips (exit 0) when either is missing, unless REQUIRE_TOOLCHAIN=1 (CI sets it).
set -euo pipefail

CXX="${CROSS_CXX:-arm-none-eabi-g++}"
NM="${CROSS_NM:-arm-none-eabi-nm}"

skip_or_fail() {
   echo "$1"
   if [ "${REQUIRE_TOOLCHAIN:-0}" = "1" ]; then
      echo "REQUIRE_TOOLCHAIN=1: treating this as a failure instead of skipping." >&2
      exit 1
   fi
   echo "Skipping the compat firmware build check (set REQUIRE_TOOLCHAIN=1 to make this a failure)."
   exit 0
}

for tool in "$CXX" "$NM"; do
   if ! command -v "$tool" >/dev/null 2>&1; then
      skip_or_fail "Cross toolchain tool not found on PATH: $tool"
   fi
done
if [ -z "${ETL_INCLUDE_DIR:-}" ] || [ ! -f "$ETL_INCLUDE_DIR/etl/span.h" ]; then
   skip_or_fail "ETL_INCLUDE_DIR is not set to the include directory of an ETL checkout (configure this project once and pass <build dir>/_deps/etl-src/include)."
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Cortex-M4F, as in check-floattext-zero-cost.sh. ETL is a system include, so the strict flags apply to libArT only.
MFLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
CXXFLAGS="-Os -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror $MFLAGS -I$ROOT/src -isystem $ETL_INCLUDE_DIR"

"$CXX" --version | head -n 1

status=0
for config in "17 0" "23 0" "23 1"; do
   read -r std use_std <<<"$config"
   object="$WORK/compat-cpp$std-$use_std.o"
   echo "== C++$std, ART_USE_STD_EXPECTED_SPAN=$use_std =="
   "$CXX" -c -std=c++$std -DART_USE_STD_EXPECTED_SPAN=$use_std $CXXFLAGS -o "$object" \
      "$ROOT/tests/firmware/CompatFirmwareBuild.cpp"
   # The exception runtime (__cxa_*, the personality routines, the unwinder, and libstdc++'s std::__throw_*()
   # helpers that a checked accessor such as std::array::at() calls even with -fno-exceptions) and the RTTI runtime.
   if "$NM" -u "$object" | grep -E "__cxa_(throw|begin_catch|end_catch|allocate_exception|rethrow)|__gxx_personality|_ZSt[0-9]+__throw_|_Unwind_|__aeabi_unwind_cpp_pr|_ZTV.*type_info|__dynamic_cast|_ZTI"; then
      echo "FAIL: the compat headers reference the exception or RTTI runtime." >&2
      status=1
   else
      echo "  compiled, no exception or RTTI runtime references - OK"
   fi
done

exit "$status"

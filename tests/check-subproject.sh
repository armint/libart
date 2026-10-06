#!/usr/bin/env bash
# Builds the superproject tests/subproject-consumer/, which adds libArT with add_subdirectory(), and checks the rule
# "one span type per build" (README.md, "span-compat"):
#
#   - C++17 with a span-compat library (as system-stm32) and SPAN_COMPAT_USE_STD_SPAN=OFF: art::span is the span of
#     span-compat, and the consumer passes it to the library. It builds and runs.
#   - C++17 without that library: libArT fetches span-compat itself. It builds and runs.
#   - C++23: libArT uses std::span and does not fetch span-compat. FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT points at a
#     directory that does not exist, so a fetch would stop the configure. It builds and runs.
#   - C++17 with the span-compat library, but SPAN_COMPAT_USE_STD_SPAN left at AUTO: the configure must fail.
#   - C++23 with the span-compat library and SPAN_COMPAT_USE_STD_SPAN=OFF: the configure must fail, because art::span
#     is std::span and span_compat::span is not.
#
# Variables: FETCHCONTENT_BASE_DIR (optional, passed to every configure), FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT
# (optional, a local span-compat checkout instead of the clone), WORK_DIR (optional, default: a new temporary
# directory; it must not be in the source tree).
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="${WORK_DIR:-$(mktemp -d)}"

common_args=(-S "$source_dir/tests/subproject-consumer" -G "Unix Makefiles" -D CMAKE_BUILD_TYPE=Release
   -D LIBART_SOURCE_DIR="$source_dir")
if [ -n "${FETCHCONTENT_BASE_DIR:-}" ]; then
   common_args+=(-D FETCHCONTENT_BASE_DIR="$FETCHCONTENT_BASE_DIR")
fi
span_compat_args=()
if [ -n "${FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT:-}" ]; then
   span_compat_args=(-D FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT="$FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT")
fi

build_and_run() {
   local name="$1"
   shift
   echo "== $name =="
   rm -rf "$work_dir/$name"
   cmake "${common_args[@]}" -B "$work_dir/$name" "$@"
   grep -E '^CXX_(DEFINES|INCLUDES)' "$work_dir/$name/CMakeFiles/art-subproject-consumer.dir/flags.make"
   cmake --build "$work_dir/$name" --target art-subproject-consumer -j"$(nproc)"
   "$work_dir/$name/art-subproject-consumer"
}

configure_must_fail() {
   local name="$1"
   local expected="$2"
   shift 2
   echo "== $name (the configure must fail) =="
   rm -rf "$work_dir/$name"
   local output
   if output=$(cmake "${common_args[@]}" -B "$work_dir/$name" "$@" 2>&1); then
      echo "$output"
      echo "FAIL: the configure passed" >&2
      exit 1
   fi
   # CMake wraps long messages, so join the lines before the match.
   if ! tr -s ' \n' ' ' <<<"$output" | grep -q "$expected"; then
      echo "$output"
      echo "FAIL: the configure failed without \"$expected\"" >&2
      exit 1
   fi
   echo "failed with \"$expected\" - OK"
}

build_and_run cpp17-hal "${span_compat_args[@]}" -D CMAKE_CXX_STANDARD=17 -D ART_CONSUMER_HAL=ON \
   -D SPAN_COMPAT_USE_STD_SPAN=OFF
build_and_run cpp17 "${span_compat_args[@]}" -D CMAKE_CXX_STANDARD=17
build_and_run cpp23 -D CMAKE_CXX_STANDARD=23 -D FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT="$work_dir/no-span-compat"
if grep -q '^SPAN_COMPAT_USE_STD_SPAN:' "$work_dir/cpp23/CMakeCache.txt"; then
   echo "FAIL: the C++23 build configured span-compat" >&2
   exit 1
fi
configure_must_fail cpp17-hal-auto "needs SPAN_COMPAT_USE_STD_SPAN=OFF, but it is 'AUTO'" "${span_compat_args[@]}" \
   -D CMAKE_CXX_STANDARD=17 -D ART_CONSUMER_HAL=ON
configure_must_fail cpp23-hal-off "but span-compat is configured with SPAN_COMPAT_USE_STD_SPAN=OFF" \
   "${span_compat_args[@]}" -D CMAKE_CXX_STANDARD=23 -D ART_CONSUMER_HAL=ON -D SPAN_COMPAT_USE_STD_SPAN=OFF

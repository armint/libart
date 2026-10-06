#!/usr/bin/env bash
# Installs art-core with `cmake --install` into a temporary prefix and builds the consumer project
# tests/install-consumer/ against it with find_package(art-core), in one of two configurations:
#
#   - CXX_STANDARD=17: libArT and the consumer at C++17, with the own types (ART_USE_STD_EXPECTED_SPAN=0);
#   - CXX_STANDARD=23: libArT at its default (C++23, std::expected and std::span). The consumer sets no standard,
#     so the C++23 it is compiled at comes from the cxx_std_23 requirement of the installed art::core.
#
# The script fails if the installed headers are not exactly the headers of src/ in git without storage/littlefs/, or
# if an installed header includes Ryu or littlefs, which are not part of the installed package. The consumer compiles
# every installed header on its own, except freertos/ and storage/fatfs/, and runs a smoke test.
#
# ETL is installed into the same prefix from the ETL source of the libArT configure (ETL installs its package only
# when it is the top-level project); ArduinoJson is installed by the `cmake --install` of libArT itself, and so is
# span-compat with the own types (configured with SPAN_COMPAT_USE_STD_SPAN=OFF). With the std types, span-compat must
# not be in the prefix.
#
# Variables: CXX_STANDARD (17 or 23, required), FETCHCONTENT_BASE_DIR (optional, passed to the libArT configure; the
# ETL source is taken from it when set), FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT (optional, a local span-compat checkout
# instead of the clone), WORK_DIR (optional, default: a new temporary directory; it must not be in the
# source tree).
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="${WORK_DIR:-$(mktemp -d)}"
build_dir="$work_dir/build-cpp$CXX_STANDARD"
prefix="$work_dir/prefix-cpp$CXX_STANDARD"
consumer_dir="$work_dir/consumer-cpp$CXX_STANDARD"

case "$CXX_STANDARD" in
   17)
      libart_args=(-D CMAKE_CXX_STANDARD=17)
      consumer_args=(-D CMAKE_CXX_STANDARD=17 -D ART_CONSUMER_EXPECT_STD_TYPES=0)
      ;;
   23)
      libart_args=()
      consumer_args=(-D ART_CONSUMER_EXPECT_STD_TYPES=1)
      ;;
   *)
      echo "CXX_STANDARD must be 17 or 23" >&2
      exit 1
      ;;
esac

fetchcontent_args=()
if [ -n "${FETCHCONTENT_BASE_DIR:-}" ]; then
   fetchcontent_args=(-D FETCHCONTENT_BASE_DIR="$FETCHCONTENT_BASE_DIR")
fi
if [ -n "${FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT:-}" ]; then
   fetchcontent_args+=(-D FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT="$FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT")
fi

rm -rf "$build_dir" "$prefix" "$consumer_dir"

echo "== Install art-core (C++$CXX_STANDARD) into $prefix =="
cmake -S "$source_dir" -B "$build_dir" -G "Unix Makefiles" -D CMAKE_BUILD_TYPE=Release "${fetchcontent_args[@]}" \
   "${libart_args[@]}"
cmake --build "$build_dir" --target art-core -j"$(nproc)"
cmake --install "$build_dir" --prefix "$prefix"

etl_source_dir="${FETCHCONTENT_BASE_DIR:-$build_dir/_deps}/etl-src"
cmake -S "$etl_source_dir" -B "$work_dir/etl-build-cpp$CXX_STANDARD" -G "Unix Makefiles"
cmake --install "$work_dir/etl-build-cpp$CXX_STANDARD" --prefix "$prefix"

echo "== Installed libArT headers =="
(cd "$prefix/include/libart" && find . -type f | sed 's|^\./||' | sort) > "$work_dir/installed-headers.txt"
cat "$work_dir/installed-headers.txt"
# The installed set must be exactly the headers of src/ in git, without storage/littlefs/: a header that the install
# rule misses, or a file that it installs in addition, fails here, also one that no other header includes.
git -C "$source_dir" ls-files 'src/*.h' 'src/*.hpp' | grep -v '^src/storage/littlefs/' | sed 's|^src/||' | sort \
   > "$work_dir/expected-headers.txt"
if ! diff -u "$work_dir/expected-headers.txt" "$work_dir/installed-headers.txt"; then
   echo "The installed headers differ from the headers of src/ (- missing, + not expected)" >&2
   exit 1
fi
# Ryu and littlefs are private to the build; an installed header must not need them.
if grep -rEn '#[[:space:]]*include[[:space:]]*[<"](ryu/|lfs\.h|lfs_util\.h)' "$prefix/include/libart"; then
   echo "An installed header includes Ryu or littlefs" >&2
   exit 1
fi
if [ -e "$prefix/include/libart/storage/littlefs" ]; then
   echo "storage/littlefs/ must not be installed" >&2
   exit 1
fi

echo "== span-compat in the prefix =="
span_compat_targets="$prefix/share/cmake/span-compat/span-compatTargets.cmake"
if [ "$CXX_STANDARD" = 17 ]; then
   grep -q 'SPAN_COMPAT_USE_STD_SPAN=0' "$span_compat_targets" || {
      echo "span-compat is not installed with SPAN_COMPAT_USE_STD_SPAN=0 ($span_compat_targets)" >&2
      exit 1
   }
   echo "installed with SPAN_COMPAT_USE_STD_SPAN=0 - OK"
else
   if [ -e "$prefix/include/span-compat" ] || [ -e "$prefix/share/cmake/span-compat" ]; then
      echo "span-compat must not be installed with the std types" >&2
      exit 1
   fi
   echo "not installed - OK"
fi

echo "== Build the consumer against the installed package =="
cmake -S "$source_dir/tests/install-consumer" -B "$consumer_dir" -G "Unix Makefiles" -D CMAKE_BUILD_TYPE=Release \
   -D CMAKE_PREFIX_PATH="$prefix" "${consumer_args[@]}"
# The flags that art::core gives the consumer: ART_USE_STD_EXPECTED_SPAN, the include paths and the -std flag.
grep -E '^CXX_(DEFINES|INCLUDES|FLAGS)' "$consumer_dir/CMakeFiles/art-install-consumer.dir/flags.make"
cmake --build "$consumer_dir" -j"$(nproc)"
"$consumer_dir/art-install-consumer"

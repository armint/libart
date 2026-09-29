# Ryu (https://github.com/ulfjack/ryu), used here under the Boost Software License 1.0 (it is
# dual-licensed Apache-2.0 / BSL-1.0; BSL needs no attribution in a distributed firmware binary,
# unlike Apache-2.0's NOTICE obligations). Backs FloatText.h/.cpp (src/utils/): correctly-rounded,
# shortest-round-trip float<->text conversion, with no newlib printf/strtod, no libm and no heap.
#
# A pinned tarball, not a git checkout: it never changes once fetched, and a tarball+hash is
# verifiable offline. Set FETCHCONTENT_SOURCE_DIR_RYU to a local checkout to build offline instead
# of fetching it. Populate only, never add_subdirectory(): Ryu's own CMakeLists.txt also builds
# the double conversion (d2s.c and friends), which this project has no use for;
# src/utils/ryu_float.c includes only the two float-only .c files it needs.
#
# Included from the top-level CMakeLists.txt (both the ESP-IDF and the standalone path) and from
# tests/CMakeLists.txt, so the URL and hash live in exactly one place.
include(FetchContent)

# Silences a CMake >=3.24 dev warning about URL downloads' extracted-file timestamps; harmless on
# older CMake (this project's minimum is 3.20), where the policy does not exist yet.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(ryu
        URL https://github.com/ulfjack/ryu/archive/4c0618b0e44f7ef027ebae05d2cc7812048f7c8f.tar.gz
        URL_HASH SHA256=5c039f2957769c5f61a8699b12d8703043b3f47c3ac6e2124c5a04afb6819f29
)
FetchContent_GetProperties(ryu)
if(NOT ryu_POPULATED)
    FetchContent_Populate(ryu)
endif()

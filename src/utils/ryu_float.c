// Wrapper translation unit for Ryu (https://github.com/ulfjack/ryu), used here under the Boost
// Software License 1.0 (it is dual-licensed Apache-2.0 / BSL-1.0; BSL needs no attribution in a
// distributed firmware binary, unlike Apache-2.0's NOTICE obligations). Fetched by CMake (see
// the `ryu` FetchContent_Declare()) rather than vendored or added as a submodule.
//
// libArT only needs float<->text conversion (FloatText.h/.cpp), never double, so this pulls in
// just f2s.c (format) and s2f.c (parse) instead of Ryu's own CMakeLists.txt target, which also
// builds the double paths (d2s.c, d2fixed.c, ...) that this project has no use for.
//
// Ryu's own assert() calls (f2s_intrinsics.h, common.h, s2f.c) pull in newlib's fiprintf ->
// _vfprintf_r/_malloc_r/__sf chain when NDEBUG is unset - which is the default in ESP-IDF, in a
// CMAKE_BUILD_TYPE=Debug build and in a build with no build type at all. That defeats "no newlib
// printf, no heap" regardless of what FloatText.cpp itself does, so NDEBUG is forced here, for
// this translation unit only, independent of the containing project's build type. Define
// LIBART_RYU_KEEP_ASSERTS to opt back into Ryu's own assertions for local debugging.
#ifndef LIBART_RYU_KEEP_ASSERTS
#ifndef NDEBUG
#define NDEBUG
#endif
#endif

// RYU_FLOAT_FULL_TABLE selects f2s_full_table.h's small, float-only pow5 tables (~800 B) instead
// of Ryu's default of reusing the double conversion's tables (+10 KB) at reduced precision.
//
// Compiling both .c files into this single TU, instead of adding them as separate source files,
// means their file-scope `static` tables (FLOAT_POW5_SPLIT, FLOAT_POW5_INV_SPLIT in
// f2s_intrinsics.h, DIGIT_TABLE in digit_table.h) are emitted once instead of once per TU.
#define RYU_FLOAT_FULL_TABLE
#include "ryu/f2s.c"
#include "ryu/s2f.c"

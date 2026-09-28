// Wrapper translation unit for Ryu (https://github.com/ulfjack/ryu), fetched by CMake (see
// FetchContent_Declare(ryu ...) in the top-level CMakeLists.txt) rather than vendored or added
// as a submodule.
//
// libArT only needs float<->text conversion (FloatText.h/.cpp), never double, so this pulls in
// just f2s.c (format) and s2f.c (parse) instead of Ryu's own CMakeLists.txt target, which also
// builds the double paths (d2s.c, d2fixed.c, ...) that this project has no use for.
//
// RYU_FLOAT_FULL_TABLE selects f2s_full_table.h's small, float-only pow5 tables (~800 B) instead
// of Ryu's default of reusing the double conversion's tables (+10 KB) at reduced precision.
//
// Compiling both .c files into this single TU, instead of adding them as separate source files,
// means their file-scope `static` tables (FLOAT_POW5_SPLIT, FLOAT_POW5_INV_SPLIT in
// f2s_intrinsics.h, DIGIT_TABLE in digit_table.h) are emitted once instead of once per TU.
#define RYU_FLOAT_FULL_TABLE
#include "ryu/f2s.c"
#include "ryu/s2f.c"

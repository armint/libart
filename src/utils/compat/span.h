#pragma once

/**
 * @file
 * @brief art::span: std::span in a build with ART_USE_STD_EXPECTED_SPAN=1, etl::span otherwise.
 *
 * See utils/compat/config.h for how the choice is made. etl::span has the same size and generates the same code as
 * std::span. It has no bounds check in operator[] either. Both are brought in with a using-declaration, not with an
 * alias template, so that class template argument deduction (art::span(pointer, size)) works at C++17.
 */

#include "utils/compat/config.h"

#if ART_USE_STD_EXPECTED_SPAN
#include <span>
#else
#include <etl/span.h>
#endif

namespace art {

#if ART_USE_STD_EXPECTED_SPAN
using std::dynamic_extent;
using std::span;
#else
using etl::dynamic_extent;
using etl::span;
#endif

} // namespace art

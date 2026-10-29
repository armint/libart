#pragma once

/**
 * @file
 * @brief art::span: std::span in a build with ART_USE_STD_EXPECTED_SPAN=1, the span of span-compat otherwise.
 *
 * See utils/compat/config.h for how the choice is made. With ART_USE_STD_EXPECTED_SPAN=0, art::span is
 * span_compat::span (https://github.com/armint/span-compat), the span that system-stm32 uses in its public interface, so
 * that both libraries pass the same type. The CMake build of art-core configures span-compat with
 * SPAN_COMPAT_USE_STD_SPAN=OFF, so span_compat::span is the own span of span-compat in every translation unit, also
 * at C++20 or later. Both are brought in with a using-declaration, not with an alias template, so that class template
 * argument deduction (art::span(pointer, size)) works at C++17.
 */

#include "utils/compat/config.h"

#if ART_USE_STD_EXPECTED_SPAN
#include <span>
#else
#include <span-compat/span.h>
#if SPAN_COMPAT_USE_STD_SPAN
#error "ART_USE_STD_EXPECTED_SPAN is 0, but span_compat::span is std::span. Build with SPAN_COMPAT_USE_STD_SPAN=OFF (CMake), so that art::span is the own span of span-compat in every translation unit."
#endif
#endif

namespace art {

#if ART_USE_STD_EXPECTED_SPAN
using std::dynamic_extent;
using std::span;
#else
using span_compat::dynamic_extent;
using span_compat::span;
#endif

} // namespace art

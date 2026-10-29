#pragma once

/**
 * @file
 * @brief Selects, once per build, whether the compat headers alias the std types or provide their own.
 *
 * ART_USE_STD_EXPECTED_SPAN decides what art::expected, art::unexpected and art::span are:
 * - 1: aliases of std::expected, std::unexpected and std::span (needs a C++23 standard library).
 * - 0: libArT's own art::expected / art::unexpected (utils/compat/expected.h) and the own span of span-compat
 *   (span_compat::span).
 *
 * The CMake build of art-core sets it as a PUBLIC compile definition (option ART_USE_STD_EXPECTED_SPAN), so that
 * libArT, libArT-proto and the application all compile with the same choice. Readable and Writable have virtual
 * functions that return expected and take span, so a program that mixes both choices violates the one-definition
 * rule without any diagnostic. Never define it per translation unit.
 *
 * A build that does not define it (the ESP-IDF component build has no CMAKE_CXX_STANDARD to derive it from) gets
 * 1 if the standard library provides std::expected and std::span, and 0 otherwise. Every translation unit of such a
 * build must therefore use the same language standard.
 *
 * The check uses the library feature-test macros instead of __cplusplus: GCC 12 reports __cplusplus == 202100L in
 * C++23 mode although its library has std::expected.
 */

#if __has_include(<version>)
#include <version>
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
#define ART_COMPAT_HAS_STD_EXPECTED_SPAN 1
#else
#define ART_COMPAT_HAS_STD_EXPECTED_SPAN 0
#endif

#ifndef ART_USE_STD_EXPECTED_SPAN
#define ART_USE_STD_EXPECTED_SPAN ART_COMPAT_HAS_STD_EXPECTED_SPAN
#endif

#if ART_USE_STD_EXPECTED_SPAN && !ART_COMPAT_HAS_STD_EXPECTED_SPAN
#error "ART_USE_STD_EXPECTED_SPAN is 1, but this translation unit has no std::expected/std::span (C++23 needed). Build every translation unit at C++23, or configure with -DART_USE_STD_EXPECTED_SPAN=OFF."
#endif

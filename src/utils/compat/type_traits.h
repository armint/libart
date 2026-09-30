#pragma once

/**
 * @file
 * @brief art::remove_cvref / art::remove_cvref_t: std::remove_cvref (C++20) for C++17.
 *
 * Always libArT's own struct, at every language standard, so that art::remove_cvref names the same entity in every
 * translation unit. An alias of std::remove_cvref in C++20 translation units and an own struct in C++17 ones would
 * give a header template that uses it two different definitions (an ODR violation), for example when libArT builds
 * at C++23 and the application at C++17. art::remove_cvref_t yields the same type as std::remove_cvref_t.
 */

#include <type_traits>

namespace art {

template <typename T>
struct remove_cvref {
   using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

} // namespace art

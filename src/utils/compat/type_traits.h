#pragma once

/**
 * @file
 * @brief art::remove_cvref / art::remove_cvref_t: std::remove_cvref (C++20) for C++17.
 *
 * An alias template is transparent, so choosing per translation unit is safe here; it does not follow
 * ART_USE_STD_EXPECTED_SPAN.
 */

#include <type_traits>
#if __has_include(<version>)
#include <version>
#endif

namespace art {

#if defined(__cpp_lib_remove_cvref)
using std::remove_cvref;
using std::remove_cvref_t;
#else
template <typename T>
struct remove_cvref {
   using type = std::remove_cv_t<std::remove_reference_t<T> >;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;
#endif

} // namespace art

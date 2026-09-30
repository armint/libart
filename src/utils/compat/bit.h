#pragma once

/**
 * @file
 * @brief art::bit_cast (std::bit_cast, C++20) and art::is_little_endian (instead of std::endian, C++20) for C++17.
 *
 * art::bit_cast is one constexpr function template on the compiler builtin __builtin_bit_cast (GCC 11, Clang 9 and
 * later, at every language standard). It is the same entity in every translation unit, whatever the language
 * standard. An alias of std::bit_cast in C++20 translation units and an own template in C++17 ones would give an
 * inline function or template in a libArT header that calls it two different definitions (an ODR violation), for
 * example when libArT builds at C++23 and the application at C++17.
 */

#include <type_traits>

#if !defined(__has_builtin)
#error "art::bit_cast needs __builtin_bit_cast (GCC 11 or Clang 9 and later)"
#elif !__has_builtin(__builtin_bit_cast)
#error "art::bit_cast needs __builtin_bit_cast (GCC 11 or Clang 9 and later)"
#endif

namespace art {

/**
 * @brief Reinterpret the object representation of @p from as a To, like std::bit_cast. Usable in constant
 * expressions under the same conditions as std::bit_cast (no pointers, unions or references in To or From).
 */
template <typename To, typename From>
constexpr std::enable_if_t<sizeof(To) == sizeof(From) && std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>, To>
bit_cast(const From &from) noexcept {
   return __builtin_bit_cast(To, from);
}

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#error "art::is_little_endian needs the GCC/Clang macros __BYTE_ORDER__, __ORDER_LITTLE_ENDIAN__ and __ORDER_BIG_ENDIAN__"
#endif

/**
 * @brief true on a little-endian target. Replaces std::endian::native == std::endian::little.
 */
inline constexpr bool is_little_endian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__,
              "mixed-endian targets are not supported");

} // namespace art

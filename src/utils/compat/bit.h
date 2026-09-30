#pragma once

/**
 * @file
 * @brief art::bit_cast (std::bit_cast, C++20) and art::is_little_endian (instead of std::endian, C++20) for C++17.
 *
 * art::bit_cast is std::bit_cast when the standard library has it. Otherwise it is a memcpy-based function
 * template that is not constexpr: do not use art::bit_cast in a constant expression. The compiler removes the
 * memcpy of such a small, fixed size, so the generated code is the same as with std::bit_cast. Both are function
 * templates without an ABI of their own, so choosing per translation unit is safe; it does not follow
 * ART_USE_STD_EXPECTED_SPAN.
 */

#include <cstring>
#include <type_traits>
#if __has_include(<version>)
#include <version>
#endif
#if defined(__cpp_lib_bit_cast)
#include <bit>
#endif

namespace art {

#if defined(__cpp_lib_bit_cast)
using std::bit_cast;
#else
/**
 * @brief Reinterpret the object representation of @p from as a To, like std::bit_cast, but not constexpr.
 *
 * Needs the same constraints as std::bit_cast, and in addition a trivially default constructible To (every
 * arithmetic type and every plain struct is).
 */
template <typename To, typename From>
std::enable_if_t<sizeof(To) == sizeof(From) && std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From> && std::is_trivially_default_constructible_v<To>, To>
bit_cast(const From &from) noexcept {
   To to;
   std::memcpy(&to, &from, sizeof(To));
   return to;
}
#endif

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

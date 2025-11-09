#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

enum class Endian { Little, Big };

/**
 * The current endianness
 */

constexpr Endian SystemEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ? Endian::Little : Endian::Big;


/** Convert a value to or from a specific endianness
 *
 * Since converting to and from are the same operation the method just checks the template
 * argument against the endianness of the architecture at compile time, if it is different are the
 * bytes swapped.
 *
 * This template uses GCC specific constants and functions that should exist in all GCC
 * implementations
 *
 * \tparam T The type of the value to convert
 * \tparam E The endianness to convert to or from
 *
 * \param in The value to convert
 *
 * \return The value converted to or from said endianness
 */
template <Endian E, typename T>
inline constexpr T convertEndian(T in) {
   static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                 "The endian conversion only works for some specific sizes");

   // If the endianness is already correct return the input value
   if(E == SystemEndian) {
      return in;
   }

   if constexpr(sizeof(T) == 1) {
      return in;
   } else if constexpr(sizeof(T) == 2) {
      if constexpr(std::is_floating_point_v<T>) {
         // float/double with 2 bytes (unlikely but handle for completeness)
         uint16_t temp;
         std::memcpy(&temp, &in, sizeof(T));
         temp = __builtin_bswap16(temp);
         T result;
         std::memcpy(&result, &temp, sizeof(T));
         return result;
      } else {
         return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(in)));
      }
   } else if constexpr(sizeof(T) == 4) {
      if constexpr(std::is_floating_point_v<T>) {
         // Handle float (32-bit)
         uint32_t temp;
         std::memcpy(&temp, &in, sizeof(T));
         temp = __builtin_bswap32(temp);
         T result;
         std::memcpy(&result, &temp, sizeof(T));
         return result;
      } else {
         return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(in)));
      }
   } else if constexpr(sizeof(T) == 8) {
      if constexpr(std::is_floating_point_v<T>) {
         // Handle double (64-bit)
         uint64_t temp;
         std::memcpy(&temp, &in, sizeof(T));
         temp = __builtin_bswap64(temp);
         T result;
         std::memcpy(&result, &temp, sizeof(T));
         return result;
      } else {
         return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(in)));
      }
   }
}

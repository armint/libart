#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

enum class Endian { Little, Big };

/**
 * The current endianness
 */

constexpr Endian SystemEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ? Endian::Little : Endian::Big;


/** Convert an integer value to or from a specific endianness
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
inline constexpr std::enable_if_t<!std::is_floating_point_v<T>, T> convertEndian(T in) {
   static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                 "The endian conversion only works for some specific sizes");

   // If the endianness is already correct return the input value
   if(E == SystemEndian) {
      return in;
   }

   if constexpr(sizeof(T) == 1) {
      return in;
   } else if constexpr(sizeof(T) == 2) {
      return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(in)));
   } else if constexpr(sizeof(T) == 4) {
      return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(in)));
   } else if constexpr(sizeof(T) == 8) {
      return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(in)));
   }
}

/** Convert a floating-point value to or from a specific endianness
 *
 * Since converting to and from are the same operation the method just checks the template
 * argument against the endianness of the architecture at compile time, if it is different are the
 * bytes swapped.
 *
 * This specialization uses memcpy to safely reinterpret floating-point values as integers
 * for byte swapping, avoiding undefined behavior.
 *
 * \tparam T The floating-point type of the value to convert
 * \tparam E The endianness to convert to or from
 *
 * \param in The value to convert
 *
 * \return The value converted to or from said endianness
 */
template <Endian E, typename T>
inline constexpr std::enable_if_t<std::is_floating_point_v<T>, T> convertEndian(T in) {
   static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                 "The endian conversion only works for 2, 4, or 8 byte floating-point types");

   // If the endianness is already correct return the input value
   if(E == SystemEndian) {
      return in;
   }

   if constexpr(sizeof(T) == 2) {
      uint16_t temp;
      std::memcpy(&temp, &in, sizeof(T));
      temp = __builtin_bswap16(temp);
      T result;
      std::memcpy(&result, &temp, sizeof(T));
      return result;
   } else if constexpr(sizeof(T) == 4) {
      uint32_t temp;
      std::memcpy(&temp, &in, sizeof(T));
      temp = __builtin_bswap32(temp);
      T result;
      std::memcpy(&result, &temp, sizeof(T));
      return result;
   } else if constexpr(sizeof(T) == 8) {
      uint64_t temp;
      std::memcpy(&temp, &in, sizeof(T));
      temp = __builtin_bswap64(temp);
      T result;
      std::memcpy(&result, &temp, sizeof(T));
      return result;
   }
}

/**
 * Convert a number's endianness and write its bytes into a byte array.
 *
 * This helper converts the input value to the requested endianness using
 * convertEndian<E, T>() and then writes the raw bytes to the destination
 * buffer. It supports both integral and floating-point types with sizes
 * of 1, 2, 4, or 8 bytes.
 *
 * \tparam E Target endianness to encode the value as
 * \tparam T The value type to convert and write
 * \param in The value to convert
 * \param out Pointer to the destination buffer with space for sizeof(T) bytes
 */
template <Endian E, typename T>
inline void convertEndianToArray(T in, uint8_t *out) {
   static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                 "The endian conversion only works for some specific sizes");
   const T tmp = convertEndian<E, T>(in);
   std::memcpy(out, &tmp, sizeof(T));
}

/**
 * Convert a number's endianness and write its bytes into a fixed-size C array
 * with optional offset and bounds checking.
 *
 * Returns false if the provided array does not have enough space starting at
 * the given offset.
 *
 * \tparam E Target endianness to encode the value as
 * \tparam T The value type to convert and write
 * \tparam N The size of the destination array
 * \param in The value to convert
 * \param out Reference to the destination array
 * \param offset Optional starting index inside the destination array
 * \return true on success, false if there is not enough space
 */
template <Endian E, typename T, size_t N>
inline bool convertEndianToArray(T in, uint8_t (&out)[N], size_t offset = 0) {
   if(N < offset || (N - offset) < sizeof(T)) {
      return false;
   }
   convertEndianToArray<E, T>(in, out + offset);
   return true;
}

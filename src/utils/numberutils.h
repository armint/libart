#pragma once
#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

enum class DecodeError { NumberTooBig, MalformedInput };
/**
 * Computes the sign of a given value.
 *
 * @param val The value whose sign is to be determined.
 * @return 1 if the value is positive, -1 if the value is negative, and 0 if the value is zero.
 */
template <typename T>
int signum(T val) {
   return (T(0) < val) - (val < T(0));
}

/**
 * @brief Computes the number of decimals in the given number.
 *
 * This function calculates the number of decimals for a given number.
 * It works with any numeric type that supports the necessary operations, such as int,
 * float, double, etc. Note that the number of decimals is calculated for the integer part
 * of the number.
 *
 * @tparam T Numeric type of the input number.
 * @param number Input number whose decimals are to be counted.
 * @return The number of decimals in the integer part of the input number.
 */
template <class T>
int numDecimals(T number) {
   if(number == 0) {
      return 1;
   }
   T num = std::abs(number);
   int decimals = 0;
   while(num > 1) {
      num /= 10;
      decimals++;
   }
   return decimals;
}

/**
 * Encodes a given value using Run-Length Encoding (RLE) and stores the
 * result in the provided destination span.
 *
 * @tparam T The type of the value to be encoded.
 * @param value The value to be encoded.
 * @param dest A span of bytes where the encoded value will be stored.
 * @return The number of bytes written to the destination span. If the
 *         destination span is too small, returns 0.
 */
template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>, T>>
size_t encodeRLE(T value, art::span<uint8_t> dest) {
   if(dest.empty()) {
      return 0;
   }
   if(value <= 0x7F) {
      dest[0] = static_cast<uint8_t>(value);
      return 1;
   }
   size_t i = 0;
   while(value != 0) {
      dest[i++] = (value & 0x7F) | 0x80;
      if(i > dest.size()) {
         return 0;
      }
      value >>= 7;
   }
   dest[i - 1] &= 0x7F; /* Unset top bit on last byte */
   return i;
}

/**
 * Encodes a given signed value using ZigZag Run-Length Encoding (RLE) and stores the
 * result in the provided destination span.
 *
 * @tparam T The type of the value to be encoded.
 * @param value The value to be encoded.
 * @param dest A span of bytes where the encoded value will be stored.
 * @return The number of bytes written to the destination span. If the
 *         destination span is too small, returns 0.
 */
template <typename T, typename = std::enable_if_t<std::is_signed_v<T>, T>>
size_t encodeRLEZigZag(T value, art::span<uint8_t> dest) {
   using unsigned_type = typename std::make_unsigned<T>::type;
   unsigned_type zigzag;
   if(value < 0) {
      zigzag = ~(static_cast<unsigned_type>(value) << 1);
   } else {
      zigzag = static_cast<unsigned_type>(value) << 1;
   }
   return encodeRLE(zigzag, dest);
}

/**
 * Decodes a Run-Length Encoded (RLE) sequence from a given source span into the destination variable.
 *
 * @param dest A reference to the variable where the decoded value will be stored.
 * @param src A span of bytes representing the RLE encoded data.
 * @return An art::expected containing the number of bytes processed from the input span if decoding was successful.
 *         If an error occurs, an art::unexpected containing a DecodeError enum value is returned.
 */
template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>, T>>
art::expected<size_t, DecodeError> decodeRLE(T &dest, art::span<uint8_t> src) {
   dest = 0;
   size_t index = 0;
   size_t bitIndex = 0;
   uint8_t byte;
   do {
      if(index >= src.size()) {
         return art::unexpected(DecodeError::MalformedInput);
      }
      if(bitIndex >= std::numeric_limits<T>::digits) {
         return art::unexpected(DecodeError::NumberTooBig);
      }
      byte = src[index];
      dest |= static_cast<T>(byte & 0x7F) << bitIndex;
      index++;
      bitIndex += 7;
   } while(byte & 0x80);
   return index;
}

/**
 * Decodes a ZigZag Run-Length Encoded (RLE) sequence from a given source span into the destination variable.
 *
 * @param dest A reference to the variable where the decoded value will be stored.
 * @param src A span of bytes representing the RLE encoded data.
 * @return An art::expected containing the number of bytes processed from the input span if decoding was successful.
 *         If an error occurs, an art::unexpected containing a DecodeError enum value is returned.
 */
template <typename T, typename = std::enable_if_t<std::is_signed_v<T>, T>>
art::expected<size_t, DecodeError> decodeRLEZigZag(T &dest, art::span<uint8_t> src) {
   using unsigned_type = typename std::make_unsigned<T>::type;
   unsigned_type value = 0;
   auto result = decodeRLE(value, src);
   if(result) {
      if(value & 1) {
         dest = static_cast<T>(~(value >> 1));
      } else {
         dest = static_cast<T>(value >> 1);
      }
   }
   return result;
}

/**
 * @brief Converts a string to an integer with a given base.
 *
 * @tparam T The integer type to convert to.
 * @param str The string view to convert.
 * @param base The base to use for conversion (default is 10).
 * @return An art::expected containing the converted value or a DecodeError.
 */
template <typename T>
art::expected<T, DecodeError> stringToInteger(std::string_view str, int base = 10) {
   if(str.empty()) {
      return art::unexpected(DecodeError::MalformedInput);
   }

   bool negative = false;
   size_t i = 0;

   if constexpr(std::is_signed_v<T>) {
      if(str[0] == '-') {
         negative = true;
         i = 1;
      } else if(str[0] == '+') {
         i = 1;
      }
   }

   if(i == str.size()) {
      return art::unexpected(DecodeError::MalformedInput);
   }

   using UnsignedT = std::make_unsigned_t<T>;
   UnsignedT result = 0;
   for(; i < str.size(); ++i) {
      uint8_t digit;
      if(str[i] >= '0' && str[i] <= '9') {
         digit = str[i] - '0';
      } else if(str[i] >= 'a' && str[i] <= 'z') {
         digit = str[i] - 'a' + 10;
      } else if(str[i] >= 'A' && str[i] <= 'Z') {
         digit = str[i] - 'A' + 10;
      } else {
         return art::unexpected(DecodeError::MalformedInput);
      }

      if(digit >= base) {
         return art::unexpected(DecodeError::MalformedInput);
      }

      if(__builtin_mul_overflow(result, static_cast<UnsignedT>(base), &result)) {
         return art::unexpected(DecodeError::NumberTooBig);
      }
      if(__builtin_add_overflow(result, static_cast<UnsignedT>(digit), &result)) {
         return art::unexpected(DecodeError::NumberTooBig);
      }
   }

   if constexpr(std::is_signed_v<T>) {
      if(negative) {
         if(result > static_cast<UnsignedT>(std::numeric_limits<T>::max()) + 1) {
            return art::unexpected(DecodeError::NumberTooBig);
         }
         return static_cast<T>(~result + 1);
      }
      if(result > static_cast<UnsignedT>(std::numeric_limits<T>::max())) {
         return art::unexpected(DecodeError::NumberTooBig);
      }
   }

   return static_cast<T>(result);
}

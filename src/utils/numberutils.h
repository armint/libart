#pragma once
#include <cmath>
#include <cstdlib>
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
 * @brief Computes the number of digits in the given number.
 *
 * This function calculates the number of digits of a given number.
 * It works with any numeric type that supports the necessary operations, such as int,
 * float, double, etc. Note that the number of digits is calculated for the integer part
 * of the number.
 *
 * @tparam T Numeric type of the input number.
 * @param number Input number whose digits are to be counted.
 * @return The number of digits in the integer part of the input number.
 */
template <class T>
int numDigits(T number) {
   if(number == 0) {
      return 1;
   }
   T num = std::abs(number);
   int digits = 0;
   while(num > 1) {
      num /= 10;
      digits++;
   }
   return digits;
}

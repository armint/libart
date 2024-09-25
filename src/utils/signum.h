#pragma once
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

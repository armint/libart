/*
 * Copyright (c) 2004,2012 Kustaa Nyholm / SpareTimeLabs
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * Neither the name of the Kustaa Nyholm or SpareTimeLabs nor the names of its
 * contributors may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
#include "tfp_printf.h"
#include <art_types.h>
#include <cstring>
#include <limits>

typedef void (*putcf)(void *, char);

int unsigned_int_to_string(big_uint_t num, int base, int use_uppercase, char *bf, int bf_size) {
   int n = 0;
   big_uint_t d = 1;
   while(num / d >= (big_uint_t)base) {
      big_uint_t next_d = d * base;
      if(next_d < d)
         break; // Overflow of d
      d = next_d;
   }
   while(d != 0) {
      uint32_t dgt = num / d;
      num %= d;
      d /= (big_uint_t)base;
      if(n || dgt > 0 || d == 0) {
         if(n < bf_size - 1) {
            *bf++ = dgt + (dgt < 10 ? '0' : (use_uppercase ? 'A' : 'a') - 10);
            ++n;
         } else {
            *bf = 0;
            return -1;
         }
      }
   }
   *bf = 0;
   return n;
}

int int_to_string(big_int_t num, char *bf, int bf_size) {
   int written = 0;
   if(num < 0) {
      if(written < bf_size - 1) {
         *bf++ = '-';
         written++;
      } else {
         return -1;
      }
      big_uint_t unum;
      if(num == std::numeric_limits<big_int_t>::min()) {
         unum = (big_uint_t)std::numeric_limits<big_int_t>::max() + 1;
      } else {
         unum = (big_uint_t)-num;
      }
      int res = unsigned_int_to_string(unum, 10, 0, bf, bf_size - written);
      if(res < 0)
         return -1;
      return written + res;
   }
   return unsigned_int_to_string((big_uint_t)num, 10, 0, bf, bf_size);
}

int get_numeric_value(char ch) {
   int val = -1;
   if(ch >= '0' && ch <= '9') {
      val = ch - '0';
   } else if(ch >= 'a' && ch <= 'z') {
      val = ch - 'a' + 10;
   } else if(ch >= 'A' && ch <= 'Z') {
      val = ch - 'A' + 10;
   }
   return val;
}

char string_to_int(char ch, const char **src, int base, int *nump) {
   const char *p = *src;
   big_uint_t num = 0;
   int digit;
   while((digit = get_numeric_value(ch)) >= 0) {
      if(digit >= base)
         break;
      big_uint_t next_num = num * (big_uint_t)base + (big_uint_t)digit;
      if(next_num < num) {
         num = (big_uint_t)std::numeric_limits<int>::max();
      } else {
         num = next_num;
      }
      ch = *p++;
   }
   *src = p;
   if(num > (big_uint_t)std::numeric_limits<int>::max()) {
      *nump = std::numeric_limits<int>::max();
   } else {
      *nump = (int)num;
   }
   return ch;
}

static void putstr(void *putp, putcf putf, const char *s) {
   while(*s)
      putf(putp, *s++);
}

static void putchw(void *putp, putcf putf, int n, char z, char *bf) {
   char fc = z ? '0' : ' ';
   char ch;
   char *p = bf;
   while(*p++ && n > 0)
      n--;
   while(n-- > 0)
      putf(putp, fc);
   while((ch = *bf++))
      putf(putp, ch);
}

void tfp_format(void *putp, putcf putf, const char *fmt, va_list va) {
#if defined(LIBART_ENABLE_64BIT)
   char bf[65];
#else
   char bf[33];
#endif
   char ch;

   while((ch = *(fmt++))) {
      if(ch != '%')
         putf(putp, ch);
      else {
         char lz = 0;
         int minChars = 0;
         int decimals = 3;
         bool l = false;
#if defined(LIBART_ENABLE_64BIT)
         bool ll = false;
#endif
         ch = *(fmt++);
         if(ch == '0') {
            ch = *(fmt++);
            lz = 1;
         }
         if(ch >= '0' && ch <= '9') {
            ch = string_to_int(ch, &fmt, 10, &minChars);
         }
         if(ch == '.') {
            ch = *(fmt++);
            if(ch >= '0' && ch <= '9') {
               ch = string_to_int(ch, &fmt, 10, &decimals);
            }
         }
         if(ch == 'l') {
            l = true;
            ch = *(fmt++);
            if(ch == 'l') {
#if defined(LIBART_ENABLE_64BIT)
               ll = true;
#endif
               ch = *(fmt++);
            }
         }
         switch(ch) {
         case 0:
            goto abort;
         case 'p':
            lz = 1;
            minChars = 8;
            // Fall through
         case 'u':
         case 'b':
         case 'x':
         case 'X': {
            bool upperCase = false;
            unsigned int base = 10;
            if(ch == 'b') {
               base = 2;
            } else if(ch != 'u') {
               base = 16;
            }
            if(ch == 'X') {
               upperCase = true;
            }

#if defined(LIBART_ENABLE_64BIT)
            if(ll) {
               unsigned_int_to_string(va_arg(va, uint64_t), base, upperCase, bf, sizeof(bf));
            } else if(l) {
               unsigned_int_to_string(va_arg(va, unsigned long), base, upperCase, bf, sizeof(bf));
            } else {
#else
            if(l) {
               unsigned_int_to_string(va_arg(va, unsigned long), base, upperCase, bf, sizeof(bf));
            } else {
#endif
               unsigned_int_to_string(va_arg(va, uint32_t), base, upperCase, bf, sizeof(bf));
            }
            putchw(putp, putf, minChars, lz, bf);
            break;
         }
         case 'i':
         // Fall through
         case 'd': {
#if defined(LIBART_ENABLE_64BIT)
            if(ll) {
               int_to_string(va_arg(va, int64_t), bf, sizeof(bf));
            } else if(l) {
               int_to_string(va_arg(va, long), bf, sizeof(bf));
            } else {
#else
            if(l) {
               int_to_string(va_arg(va, long), bf, sizeof(bf));
            } else {
#endif
               int_to_string(va_arg(va, int32_t), bf, sizeof(bf));
            }
            putchw(putp, putf, minChars, lz, bf);
            break;
         }
         case 'c':
            putf(putp, (char)(va_arg(va, int)));
            break;
         case 's':
            putchw(putp, putf, minChars, 0, va_arg(va, char *));
            break;
#if defined(PRINTF_ENABLE_FLOATING_POINT)
         case 'f':
            // Fall through
         case 'F': {
            float num = (float)va_arg(va, double);

            // Classify via the raw bit pattern instead of std::isnan/std::isinf
            // so this stays a plain bit test with no libm dependency.
            uint32_t bits;
            static_assert(sizeof(bits) == sizeof(num), "expected IEEE-754 32-bit float");
            std::memcpy(&bits, &num, sizeof(bits));
            const bool negative = (bits >> 31) != 0;
            const uint32_t exponent = (bits >> 23) & 0xFFu;
            const uint32_t mantissa = bits & 0x7FFFFFu;

            if(exponent == 0xFFu) {
               if(negative) {
                  putf(putp, '-');
               }
               putstr(putp, putf, mantissa != 0 ? "nan" : "inf");
               break;
            }

            float magnitude = negative ? -num : num;

            // Values that don't fit in big_uint_t would make the cast below
            // undefined behaviour; report them with a distinct marker instead.
#if defined(LIBART_ENABLE_64BIT)
            constexpr float kOverflowBound = 18446744073709551616.0f; // 2^64
#else
            constexpr float kOverflowBound = 4294967296.0f; // 2^32
#endif
            if(!(magnitude < kOverflowBound)) {
               if(negative) {
                  putf(putp, '-');
               }
               putstr(putp, putf, "ovf");
               break;
            }

            // Cap the requested precision at 9 decimals (independent of
            // big_uint_t's width) so the power-of-ten scale below cannot
            // overflow it.
            constexpr int kMaxDecimals = 9;
            if(decimals > kMaxDecimals) {
               decimals = kMaxDecimals;
            }

            // Integer power of ten, computed without powf()/libm.
            big_uint_t scale = 1;
            for(int i = 0; i < decimals; ++i) {
               scale *= 10;
            }

            big_uint_t ipart = (big_uint_t)magnitude;
            float frac = magnitude - (float)ipart;
            // Round to nearest at the last requested decimal digit, from the
            // leftover (scaled - truncated) rather than truncating
            // (scaled + 0.5f): the addition itself can round the
            // intermediate sum up to the next integer for a fraction just
            // below the halfway point, which would carry incorrectly.
            float scaledFrac = frac * (float)scale;
            big_uint_t fracPart = (big_uint_t)scaledFrac;
            float leftover = scaledFrac - (float)fracPart;
            if(leftover >= 0.5f) {
               fracPart += 1;
            }
            if(fracPart >= scale) {
               fracPart -= scale;
               if(ipart == std::numeric_limits<big_uint_t>::max()) {
                  // Rounding carried out of the representable integer range.
                  if(negative) {
                     putf(putp, '-');
                  }
                  putstr(putp, putf, "ovf");
                  break;
               }
               ipart += 1;
            }

            // The sign goes into bf, ahead of the digits, so putchw()'s
            // width/zero-fill padding treats it as part of the field (same
            // as int_to_string() already does for %d) instead of printing
            // it before the padding.
            char *p = bf;
            if(negative) {
               *p++ = '-';
            }
            int room = (int)sizeof(bf) - (int)(p - bf);
            unsigned_int_to_string(ipart, 10, 0, p, room);
            putchw(putp, putf, minChars, lz, bf);
            putf(putp, '.');
            unsigned_int_to_string(fracPart, 10, 0, bf, sizeof(bf));
            putchw(putp, putf, decimals, 1, bf);
         } break;
#endif
         case '%':
            putf(putp, ch);
         // Fall through
         default:
            break;
         }
      }
   }
abort:;
}

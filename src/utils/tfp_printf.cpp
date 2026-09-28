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
#include <cmath>
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
            float num = va_arg(va, double);
            int_to_string(truncf(num), bf, sizeof(bf));
            putchw(putp, putf, minChars, lz, bf);
            putf(putp, '.');
            num -= truncf(num);
            float rest = num * powf(10.0f, (float)decimals);
            int_to_string(truncf(rest), bf, sizeof(bf));
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

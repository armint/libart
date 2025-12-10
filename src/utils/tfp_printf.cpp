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
#include <cmath>
#if defined(PRINTF_ENABLE_64BIT)
typedef uint64_t printf_uint_t;
typedef int64_t printf_int_t;
#else
typedef uint32_t printf_uint_t;
typedef int32_t printf_int_t;
#endif
typedef void (*putcf)(void *, char);

static void ui2a(printf_uint_t num, printf_uint_t base, int uc, char *bf) {
   int n = 0;
   printf_uint_t d = 1;
   while(num / d >= base)
      d *= base;
   while(d != 0) {
      uint32_t dgt = num / d;
      num %= d;
      d /= base;
      if(n || dgt > 0 || d == 0) {
         *bf++ = dgt + (dgt < 10 ? '0' : (uc ? 'A' : 'a') - 10);
         ++n;
      }
   }
   *bf = 0;
}

static void i2a(printf_int_t num, char *bf) {
   if(num < 0) {
      num = -num;
      *bf++ = '-';
   }
   ui2a(num, 10, 0, bf);
}

int a2d(char ch) {
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

char a2i(char ch, const char **src, int base, int *nump) {
   const char *p = *src;
   int num = 0;
   int digit;
   while((digit = a2d(ch)) >= 0) {
      if(digit >= base)
         break;
      num = num * base + digit;
      ch = *p++;
   }
   *src = p;
   *nump = num;
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
   char bf[65];

   char ch;

   while((ch = *(fmt++))) {
      if(ch != '%')
         putf(putp, ch);
      else {
         char lz = 0;
         int minChars = 0;
         int decimals = 3;
         bool ll = false;
         ch = *(fmt++);
         if(ch == '0') {
            ch = *(fmt++);
            lz = 1;
         }
         if(ch >= '0' && ch <= '9') {
            ch = a2i(ch, &fmt, 10, &minChars);
         }
         if(ch == '.') {
            ch = *(fmt++);
            if(ch >= '0' && ch <= '9') {
               ch = a2i(ch, &fmt, 10, &decimals);
            }
         }
         if(ch == 'l') {
            ch = *(fmt++);
            if(ch == 'l') {
               ll = true;
               ch = *(fmt++);
            }
         }
         switch(ch) {
         case 0:
            goto abort;
         case 'u': {
#if defined(PRINTF_ENABLE_64BIT)
            if(ll) {
               ui2a(va_arg(va, uint64_t), 10, 0, bf);
            } else {
#else
            {
#endif
               ui2a(va_arg(va, uint32_t), 10, 0, bf);
            }
            putchw(putp, putf, minChars, lz, bf);
            break;
         }
         case 'b': {
#if defined(PRINTF_ENABLE_64BIT)
            if(ll) {
               ui2a(va_arg(va, uint64_t), 2, 0, bf);
            } else {
#else
            {
#endif
               ui2a(va_arg(va, uint32_t), 2, 0, bf);
            }
            putchw(putp, putf, minChars, lz, bf);
            break;
         }
         case 'i':
            // Fall through
         case 'd': {
#if defined(PRINTF_ENABLE_64BIT)
            if(ll) {
               i2a(va_arg(va, int64_t), bf);
            } else {
#else
            {
#endif
               i2a(va_arg(va, int32_t), bf);
            }
            putchw(putp, putf, minChars, lz, bf);
            break;
         }
         case 'p':
            // Pointer
            lz = 1;
            minChars = 8;
            // Fall through
         case 'x':
            // Fall through
         case 'X':
#if defined(PRINTF_ENABLE_64BIT)
            if(ll) {
               ui2a(va_arg(va, printf_uint_t), 16, (ch == 'X'), bf);
            } else {
#else
         {
#endif
               ui2a(va_arg(va, uint32_t), 16, (ch == 'X'), bf);
            }
            putchw(putp, putf, minChars, lz, bf);
            break;
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
            i2a(truncf(num), bf);
            putchw(putp, putf, minChars, lz, bf);
            putf(putp, '.');
            num -= truncf(num);
            float rest = num * powf(10.0f, decimals);
            i2a(truncf(rest), bf);
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

typedef struct {
   char *target;
   size_t pos;
   size_t max;
} snprintf_helper;

static void snputc(void *p, char c) {
   auto *h = static_cast<snprintf_helper *>(p);
   if(h->pos < h->max - 1) {
      h->target[h->pos++] = c;
   } else {
      h->target[h->max - 1] = 0;
      h->pos = h->max;
   }
}

size_t tfp_snprintf(char *s, size_t len, const char *fmt, ...) {
   va_list va;
   va_start(va, fmt);
   size_t result = tfp_vsnprintf(s, len, fmt, va);
   va_end(va);
   return result;
}

size_t tfp_vsnprintf(char *s, size_t len, const char *format, va_list va_l) {
   snprintf_helper h;

   h.target = s;
   h.pos = 0;
   h.max = len;
   tfp_format(&h, snputc, format, va_l);
   if(h.pos < len) {
      h.target[h.pos] = 0;
   }
   return h.pos;
}

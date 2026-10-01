#include "FloatText.h"
#include <bit>
#include <cstdint>

extern "C" {
#include "ryu/ryu_parse.h"
// Defined in ryu_float.c; exposes Ryu's static f2d() (see the comment there).
void art_ryu_f2d(uint32_t ieeeMantissa, uint32_t ieeeExponent, uint32_t *mantissaOut, int32_t *exponentOut);
}

namespace utils::FloatText {

namespace {

// Digit-by-digit, mantissa*10^k lookup used to write a decimal mantissa without a division or
// modulo instruction (some of this project's targets have no hardware divider).
constexpr uint32_t kPow10[9] = { 100000000u, 10000000u, 1000000u, 100000u, 10000u, 1000u, 100u, 10u, 1u };

// Writes the decimal digits of m (1..999999999, i.e. never called with m == 0) with no leading
// zeros, and returns how many digits it wrote (1..9).
size_t writeDigits(uint32_t m, char *out) {
   size_t k = 0;
   while(kPow10[k] > m) {
      k++;
   }
   size_t n = 0;
   for(; k < 9; ++k) {
      char digit = '0';
      while(m >= kPow10[k]) {
         m -= kPow10[k];
         digit++;
      }
      out[n++] = digit;
   }
   return n;
}

// Writes e (0..99) as exactly two decimal digits, no division.
void writeTwoDigits(uint32_t e, char *out) {
   char tens = '0';
   while(e >= 10) {
      e -= 10;
      tens++;
   }
   out[0] = tens;
   out[1] = static_cast<char>('0' + e);
}

// Case-insensitive exact match (ASCII only): lowercases with `| 0x20`, which only ever touches
// the 'A'-'Z' bit and leaves digits/punctuation unchanged.
bool iequals(std::string_view text, const char *lower) {
   size_t i = 0;
   for(; lower[i] != '\0'; ++i) {
      if(i >= text.size() || (text[i] | 0x20) != lower[i]) {
         return false;
      }
   }
   return i == text.size();
}

float bitsToFloat(uint32_t bits) {
   return std::bit_cast<float>(bits);
}

} // namespace

size_t formatFloat(float value, art::span<char> dest) {
   const uint32_t bits = std::bit_cast<uint32_t>(value);
   const uint32_t ieeeExponent = (bits >> 23) & 0xFFu;
   const uint32_t ieeeMantissa = bits & 0x7FFFFFu;

   char buf[kMaxTextLength];
   size_t len = 0;

   if(ieeeExponent == 0xFFu && ieeeMantissa != 0u) {
      // NaN: Ryu's own text form drops the sign bit too (there is exactly one bit pattern's
      // worth of information - "is it a NaN" - worth reporting in text; the payload does not
      // round-trip through text either way).
      buf[len++] = 'n';
      buf[len++] = 'a';
      buf[len++] = 'n';
   } else {
      if(bits >> 31) {
         buf[len++] = '-';
      }
      if(ieeeExponent == 0xFFu) { // infinity (mantissa == 0, checked by the branch above)
         buf[len++] = 'i';
         buf[len++] = 'n';
         buf[len++] = 'f';
      } else if((bits << 1) == 0u) { // +-0.0 (exponent and mantissa both zero)
         buf[len++] = '0';
      } else {
         uint32_t mantissa = 0;
         int32_t exponent = 0;
         art_ryu_f2d(ieeeMantissa, ieeeExponent, &mantissa, &exponent);
         char digits[9];
         const size_t ndigits = writeDigits(mantissa, digits);
         // Normalized scientific exponent: value == digits[0].digits[1:] * 10^exp.
         const int32_t exp = exponent + static_cast<int32_t>(ndigits) - 1;

         // %g-style threshold: fixed-point while the value fits in at most the 9 significant
         // digits a float ever needs (exp < 9), scientific below 1e-4 (exp < -4) as usual.
         if(exp >= -4 && exp < 9) {
            if(exp >= 0) {
               const size_t intDigits = static_cast<size_t>(exp) + 1;
               for(size_t k = 0; k < intDigits; ++k) {
                  buf[len++] = k < ndigits ? digits[k] : '0';
               }
               if(ndigits > intDigits) {
                  buf[len++] = '.';
                  for(size_t k = intDigits; k < ndigits; ++k) {
                     buf[len++] = digits[k];
                  }
               }
            } else {
               buf[len++] = '0';
               buf[len++] = '.';
               for(int32_t k = -1; k > exp; --k) {
                  buf[len++] = '0';
               }
               for(size_t k = 0; k < ndigits; ++k) {
                  buf[len++] = digits[k];
               }
            }
         } else {
            buf[len++] = digits[0];
            if(ndigits > 1) {
               buf[len++] = '.';
               for(size_t k = 1; k < ndigits; ++k) {
                  buf[len++] = digits[k];
               }
            }
            buf[len++] = 'e';
            buf[len++] = exp < 0 ? '-' : '+';
            writeTwoDigits(static_cast<uint32_t>(exp < 0 ? -exp : exp), buf + len);
            len += 2;
         }
      }
   }

   if(len > dest.size()) {
      return 0;
   }
   for(size_t k = 0; k < len; ++k) {
      dest[k] = buf[k];
   }
   return len;
}

art::expected<float, DecodeError> parseFloat(std::string_view text) {
   size_t i = 0;
   bool negative = false;
   if(i < text.size() && (text[i] == '-' || text[i] == '+')) {
      negative = text[i++] == '-';
   }
   // Built from a pointer and length rather than text.substr(i): substr() is bounds-checked and
   // throws on misuse, which (even though i can never exceed text.size() here) still pulls the
   // exception-throwing path, and everything it drags in, into the link.
   std::string_view rest(text.data() + i, text.size() - i);
   const uint32_t sign = negative ? 0x80000000u : 0u;

   if(iequals(rest, "inf") || iequals(rest, "infinity")) {
      return bitsToFloat(sign | 0x7F800000u);
   }
   if(iequals(rest, "nan")) {
      return bitsToFloat(0x7FC00000u);
   }
   if(!rest.empty() && (rest.back() | 0x20) == 'f') {
      rest.remove_suffix(1);
   }

   // Single left-to-right pass: build value == mantissa * 10^exponent as we go, keeping at most
   // the 9 significant digits s2f accepts. mantissa==0 && exponent==0 for every digit seen so far
   // means "still in leading zeros", which is how leading zeros (in either the integer or the
   // fractional part) are told apart from a significant digit without a separate pre-scan.
   uint32_t mantissa = 0;
   int significantDigits = 0;
   int32_t exponent = 0;
   int roundDigit = -1; // first digit dropped past the 9th significant one, or -1 if none yet
   bool sticky = false; // any nonzero digit dropped after roundDigit
   bool sawDigit = false;
   bool sawDot = false;

   size_t k = 0;
   for(; k < rest.size(); ++k) {
      const char c = rest[k];
      if(c == '.') {
         if(sawDot) {
            return art::unexpected(DecodeError::MalformedInput);
         }
         sawDot = true;
         continue;
      }
      if(c < '0' || c > '9') {
         break;
      }
      sawDigit = true;
      const uint32_t digit = static_cast<uint32_t>(c - '0');
      if(significantDigits < 9) {
         if(mantissa == 0 && digit == 0) {
            // An insignificant leading zero. One after the point still shifts the exponent
            // (0.0123 has one fewer significant leftward shift than 0.123); one before it, or
            // before any digit at all, does not.
            if(sawDot) {
               exponent--;
            }
            continue;
         }
         mantissa = mantissa * 10 + digit;
         significantDigits++;
         if(sawDot) {
            exponent--;
         }
      } else {
         // Beyond the 9 significant digits s2f accepts. A digit here still in the integer part
         // (no '.' yet) represents a higher place value that the fixed-width mantissa above
         // cannot hold, so it must still shift the exponent up; one in the fractional part is
         // just extra precision being rounded away and does not.
         if(!sawDot) {
            exponent++;
         }
         if(roundDigit < 0) {
            roundDigit = static_cast<int>(digit);
         } else {
            sticky |= digit != 0;
         }
      }
   }
   if(!sawDigit) {
      return art::unexpected(DecodeError::MalformedInput);
   }

   if(k < rest.size()) {
      if((rest[k] | 0x20) != 'e') {
         return art::unexpected(DecodeError::MalformedInput);
      }
      ++k;
      bool exponentNegative = false;
      if(k < rest.size() && (rest[k] == '-' || rest[k] == '+')) {
         exponentNegative = rest[k++] == '-';
      }
      if(k == rest.size()) {
         return art::unexpected(DecodeError::MalformedInput);
      }
      int32_t literalExponent = 0;
      for(; k < rest.size(); ++k) {
         const char c = rest[k];
         if(c < '0' || c > '9') {
            return art::unexpected(DecodeError::MalformedInput);
         }
         // Saturate rather than overflow: once past this, the result is 0 or +-inf regardless of
         // any further digits, which the magnitude check below still gets right.
         if(literalExponent < 100000) {
            literalExponent = literalExponent * 10 + (c - '0');
         }
      }
      exponent += exponentNegative ? -literalExponent : literalExponent;
   }

   // s2f rejects mantissas with 10 or more significant digits outright, so a literal with more
   // than 9 is rounded to 9 first (round-half-to-even on the first dropped digit, with a sticky
   // bit for the rest) - see FloatText.h for the resulting, measured 1-ULP error rate on such
   // literals.
   if(roundDigit > 5 || (roundDigit == 5 && (sticky || (mantissa & 1) != 0))) {
      if(++mantissa == 1000000000u) {
         // Carried into a 10th digit; drop the trailing zero it introduced instead.
         mantissa = 100000000u;
         exponent++;
      }
   }
   if(mantissa == 0) {
      return bitsToFloat(sign);
   }

   // Replicate s2f_n's own out-of-range shortcuts (see s2f.c) before constructing its input: this
   // keeps the exponent we hand it small, regardless of how the original literal was written
   // (e.g. "1" followed by 60 zeros and a huge negative exponent, or a saturated one above).
   char buf[16];
   size_t len = writeDigits(mantissa, buf);
   const int32_t magnitude = static_cast<int32_t>(len) + exponent;
   if(magnitude <= -46) {
      return bitsToFloat(sign);
   }
   if(magnitude >= 40) {
      return bitsToFloat(sign | 0x7F800000u);
   }

   buf[len++] = 'e';
   if(exponent < 0) {
      buf[len++] = '-';
      exponent = -exponent;
   }
   writeTwoDigits(static_cast<uint32_t>(exponent), buf + len);
   len += 2;

   float result = 0.0f;
   if(s2f_n(buf, static_cast<int>(len), &result) != SUCCESS) {
      // Should not happen given the normalization above; fail safe rather than propagate a wrong
      // value.
      return art::unexpected(DecodeError::MalformedInput);
   }
   return negative ? -result : result;
}

} // namespace utils::FloatText

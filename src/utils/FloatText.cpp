#include "FloatText.h"
#include <bit>
#include <cstdint>

extern "C" {
#include "ryu/ryu.h"
#include "ryu/ryu_parse.h"
}

namespace utils::FloatText {

namespace {

bool iequals(std::string_view a, std::string_view b) {
   if(a.size() != b.size()) {
      return false;
   }
   for(size_t i = 0; i < a.size(); ++i) {
      char ca = a[i];
      char cb = b[i];
      if(ca >= 'A' && ca <= 'Z') {
         ca = static_cast<char>(ca - 'A' + 'a');
      }
      if(cb >= 'A' && cb <= 'Z') {
         cb = static_cast<char>(cb - 'A' + 'a');
      }
      if(ca != cb) {
         return false;
      }
   }
   return true;
}

float bitsToFloat(uint32_t bits) {
   return std::bit_cast<float>(bits);
}

float makeInf(bool negative) {
   return bitsToFloat(0x7F800000u | (negative ? 0x80000000u : 0u));
}

float makeNan() {
   return bitsToFloat(0x7FC00000u);
}

float makeZero(bool negative) {
   return bitsToFloat(negative ? 0x80000000u : 0u);
}

} // namespace

size_t formatFloat(float value, std::span<char> dest) {
   // f2s_buffered_n never null-terminates and, per Ryu's own f2s() (which malloc(16)s), never
   // writes more than 15 characters; give it a little headroom regardless.
   char ryuBuf[16];
   const int ryuLen = f2s_buffered_n(value, ryuBuf);
   const std::string_view ryuText(ryuBuf, static_cast<size_t>(ryuLen));

   char buf[kMaxTextLength];
   size_t len = 0;

   // A numeric result never starts with 'N' or 'I' (only a digit or a '-' followed by a digit),
   // so a single leading character tells the special cases apart from a normal value without
   // pulling in string comparison machinery for it.
   if(ryuText[0] == 'N') { // "NaN"
      buf[len++] = 'n';
      buf[len++] = 'a';
      buf[len++] = 'n';
   } else if(ryuText[0] == 'I' || ryuText[1] == 'I') { // "Infinity" or "-Infinity"
      if(ryuText[0] == '-') {
         buf[len++] = '-';
      }
      buf[len++] = 'i';
      buf[len++] = 'n';
      buf[len++] = 'f';
   } else {
      // Reformat Ryu's normalized scientific notation ("[-]D[.DDD]E[-]DD") into %g style.
      size_t i = 0;
      bool negative = false;
      if(ryuText[i] == '-') {
         negative = true;
         i++;
      }
      char digits[9];
      size_t ndigits = 0;
      while(ryuText[i] != 'E') {
         if(ryuText[i] != '.') {
            digits[ndigits++] = ryuText[i];
         }
         i++;
      }
      i++; // skip 'E'
      bool expNegative = false;
      if(ryuText[i] == '-') {
         expNegative = true;
         i++;
      }
      int exp = 0;
      for(; i < ryuText.size(); ++i) {
         exp = exp * 10 + (ryuText[i] - '0');
      }
      if(expNegative) {
         exp = -exp;
      }

      if(negative) {
         buf[len++] = '-';
      }
      // %g-style threshold: fixed-point while the value fits in at most the 9 significant
      // digits a float ever needs (exp < 9), scientific below 1e-4 (exp < -4) as usual.
      const bool useFixed = exp >= -4 && exp < 9;
      if(useFixed) {
         if(exp >= 0) {
            const size_t intDigits = static_cast<size_t>(exp) + 1;
            if(ndigits <= intDigits) {
               for(size_t k = 0; k < ndigits; ++k) {
                  buf[len++] = digits[k];
               }
               for(size_t k = ndigits; k < intDigits; ++k) {
                  buf[len++] = '0';
               }
            } else {
               for(size_t k = 0; k < intDigits; ++k) {
                  buf[len++] = digits[k];
               }
               buf[len++] = '.';
               for(size_t k = intDigits; k < ndigits; ++k) {
                  buf[len++] = digits[k];
               }
            }
         } else {
            buf[len++] = '0';
            buf[len++] = '.';
            const size_t zeros = static_cast<size_t>(-exp - 1);
            for(size_t k = 0; k < zeros; ++k) {
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
         int e = exp;
         if(e < 0) {
            buf[len++] = '-';
            e = -e;
         } else {
            buf[len++] = '+';
         }
         // Zero-padded to 2 digits; a float's decimal exponent never needs a 3rd (see
         // kMaxTextLength).
         if(e < 10) {
            buf[len++] = '0';
            buf[len++] = static_cast<char>('0' + e);
         } else {
            buf[len++] = static_cast<char>('0' + e / 10);
            buf[len++] = static_cast<char>('0' + e % 10);
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

std::expected<float, DecodeError> parseFloat(std::string_view text) {
   // Defensive cap: no literal formatFloat() produces, or anyone would reasonably hand-write,
   // comes anywhere near this long; keeps the scan below bounded instead of unbounded on
   // attacker-supplied input.
   if(text.empty() || text.size() > 64) {
      return std::unexpected(DecodeError::MalformedInput);
   }

   size_t i = 0;
   bool negative = false;
   if(text[0] == '-') {
      negative = true;
      i = 1;
   } else if(text[0] == '+') {
      i = 1;
   }

   // Built from a pointer and length rather than text.substr(i): substr() is bounds-checked and
   // throws on misuse, which (even though i can never exceed text.size() here) still pulls the
   // exception-throwing path, and everything it drags in, into the link.
   std::string_view rest(text.data() + i, text.size() - i);
   if(rest.empty()) {
      return std::unexpected(DecodeError::MalformedInput);
   }

   if(iequals(rest, "inf") || iequals(rest, "infinity")) {
      return makeInf(negative);
   }
   if(iequals(rest, "nan")) {
      return makeNan();
   }

   // Optional trailing f/F suffix on a numeric literal (not on inf/nan, handled above already).
   if(rest.back() == 'f' || rest.back() == 'F') {
      rest.remove_suffix(1);
      if(rest.empty()) {
         return std::unexpected(DecodeError::MalformedInput);
      }
   }

   // Grammar: digits ['.' digits] [('e'|'E') ['+'|'-'] digits], with at least one digit on
   // either side of the '.'. Everything else, including a garbled/incomplete exponent, is
   // malformed input.
   size_t ePos = std::string_view::npos;
   for(size_t k = 0; k < rest.size(); ++k) {
      if(rest[k] == 'e' || rest[k] == 'E') {
         ePos = k;
         break;
      }
   }
   // Sliced with pointer/length instead of substr(), for the same reason as rest above.
   const std::string_view mantissaPart =
      ePos == std::string_view::npos ? rest : std::string_view(rest.data(), ePos);
   const std::string_view expPart =
      ePos == std::string_view::npos ? std::string_view{} : std::string_view(rest.data() + ePos + 1, rest.size() - ePos - 1);
   if(ePos != std::string_view::npos && expPart.empty()) {
      return std::unexpected(DecodeError::MalformedInput);
   }

   std::string_view intPart = mantissaPart;
   std::string_view fracPart;
   size_t dotPos = std::string_view::npos;
   for(size_t k = 0; k < mantissaPart.size(); ++k) {
      if(mantissaPart[k] == '.') {
         dotPos = k;
         break;
      }
   }
   if(dotPos != std::string_view::npos) {
      intPart = std::string_view(mantissaPart.data(), dotPos);
      fracPart = std::string_view(mantissaPart.data() + dotPos + 1, mantissaPart.size() - dotPos - 1);
   }
   if(intPart.empty() && fracPart.empty()) {
      return std::unexpected(DecodeError::MalformedInput);
   }
   for(char c : intPart) {
      if(c < '0' || c > '9') {
         return std::unexpected(DecodeError::MalformedInput);
      }
   }
   for(char c : fracPart) {
      if(c < '0' || c > '9') {
         return std::unexpected(DecodeError::MalformedInput);
      }
   }

   int32_t expValue = 0;
   if(!expPart.empty()) {
      const auto parsedExp = stringToInteger<int32_t>(expPart);
      if(!parsedExp) {
         return std::unexpected(parsedExp.error());
      }
      expValue = *parsedExp;
   }

   // Concatenate the integer and fractional digits (dropping the decimal point itself), and
   // locate the significant run: everything from the first to the last non-zero digit.
   char digits[64];
   size_t nd = 0;
   for(char c : intPart) {
      digits[nd++] = c;
   }
   for(char c : fracPart) {
      digits[nd++] = c;
   }
   const int64_t fracLen = static_cast<int64_t>(fracPart.size());

   size_t first = 0;
   while(first < nd && digits[first] == '0') {
      first++;
   }
   if(first == nd) {
      return makeZero(negative);
   }
   size_t last = nd - 1;
   while(digits[last] == '0') {
      last--;
   }

   const size_t sigLen = last - first + 1;
   // value == digits[first..last] (as an integer) * 10^finalExp.
   int64_t finalExp = static_cast<int64_t>(expValue) - fracLen + static_cast<int64_t>(nd - 1 - last);

   uint32_t mantissa = 0;
   size_t usedDigits;
   if(sigLen <= 9) {
      for(size_t k = first; k <= last; ++k) {
         mantissa = mantissa * 10 + static_cast<uint32_t>(digits[k] - '0');
      }
      usedDigits = sigLen;
   } else {
      // s2f rejects 9+ significant mantissa digits outright, so round the excess away:
      // round-half-to-even on the 10th significant digit, with a sticky bit for the rest.
      for(size_t k = 0; k < 9; ++k) {
         mantissa = mantissa * 10 + static_cast<uint32_t>(digits[first + k] - '0');
      }
      const int roundDigit = digits[first + 9] - '0';
      bool sticky = false;
      for(size_t k = first + 10; k <= last; ++k) {
         if(digits[k] != '0') {
            sticky = true;
            break;
         }
      }
      const bool roundUp = roundDigit > 5 || (roundDigit == 5 && (sticky || (mantissa & 1) != 0));
      int64_t extraExp = static_cast<int64_t>(sigLen - 9);
      if(roundUp) {
         mantissa++;
         if(mantissa == 1000000000u) {
            // Carried into a 10th digit; drop the trailing zero it introduced instead.
            mantissa = 100000000u;
            extraExp++;
         }
      }
      finalExp += extraExp;
      usedDigits = 9;
   }

   // Replicate s2f_n's own out-of-range shortcuts (see s2f.c) before constructing its input:
   // this keeps the exponent we hand it small, regardless of how the original literal was
   // written (e.g. "1" followed by 60 zeros and a huge negative exponent).
   const int64_t magnitude = static_cast<int64_t>(usedDigits) + finalExp;
   if(magnitude <= -46) {
      return makeZero(negative);
   }
   if(magnitude >= 40) {
      return makeInf(negative);
   }

   // Build "<mantissa>e<finalExp>" for s2f_n: at most 9 mantissa digits and, thanks to the
   // range check above, at most a 2-digit exponent.
   char buf[24];
   size_t len = 0;
   {
      char tmp[9];
      size_t tn = 0;
      uint32_t m = mantissa;
      do {
         tmp[tn++] = static_cast<char>('0' + m % 10);
         m /= 10;
      } while(m != 0);
      while(tn > 0) {
         buf[len++] = tmp[--tn];
      }
   }
   buf[len++] = 'e';
   // The magnitude check above guarantees finalExp fits comfortably in an int32_t (roughly
   // -55..39); narrow it before the digit-extraction loop below so it stays 32-bit division
   // (64-bit div/mod has no hardware support on most of this project's targets and pulls in a
   // sizeable software implementation).
   int32_t e = static_cast<int32_t>(finalExp);
   if(e < 0) {
      buf[len++] = '-';
      e = -e;
   }
   {
      char tmp[8];
      size_t tn = 0;
      uint32_t ee = static_cast<uint32_t>(e);
      do {
         tmp[tn++] = static_cast<char>('0' + ee % 10);
         ee /= 10;
      } while(ee != 0);
      while(tn > 0) {
         buf[len++] = tmp[--tn];
      }
   }

   float magnitudeResult = 0.0f;
   const enum Status status = s2f_n(buf, static_cast<int>(len), &magnitudeResult);
   if(status != SUCCESS) {
      // Should not happen given the normalization above; fail safe rather than propagate a
      // wrong value.
      return std::unexpected(DecodeError::MalformedInput);
   }
   return negative ? -magnitudeResult : magnitudeResult;
}

} // namespace utils::FloatText

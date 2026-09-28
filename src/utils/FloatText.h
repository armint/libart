#pragma once
#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <utils/numberutils.h>

namespace utils::FloatText {

// Only IEEE-754 binary32 is supported; Ryu's bit-level decoding assumes this layout.
static_assert(std::numeric_limits<float>::is_iec559, "FloatText requires IEEE-754 float");

/**
 * Upper bound on the number of characters formatFloat() can write, for any finite, infinite or
 * NaN float value. Sized for the worst case: a sign, one leading digit, a decimal point, the
 * remaining 8 digits of a 9-significant-digit mantissa (the most Ryu ever emits for a float),
 * an 'e', an exponent sign and a 2-digit exponent (float's decimal exponent never needs a 3rd
 * digit: Ryu's own comment on floating_decimal_32 puts the range at -45..38). Verified empirically
 * by TestFloatText.cpp's exhaustive round-trip test, which asserts the length it observes never
 * exceeds this.
 */
inline constexpr size_t kMaxTextLength = 15;

/**
 * Formats a float as portable, human-readable text: %g-style (fixed-point for magnitudes roughly
 * between 1e-4 and 1e9, scientific notation with a lowercase 'e' and a signed, zero-padded 2-digit
 * exponent otherwise), plus explicit "inf", "-inf" and "nan". Uses Ryu (f2s) for a correctly
 * rounded, shortest round-trip decimal representation, then reformats it; no newlib printf, no
 * libm, no heap.
 *
 * The output never has a trailing '\0'; the caller decides whether it needs one.
 *
 * @param value the float to format.
 * @param dest the buffer to write into. kMaxTextLength bytes is always enough.
 * @return the number of characters written, or 0 if dest is too small.
 */
size_t formatFloat(float value, std::span<char> dest);

/**
 * Parses text produced by formatFloat(), or a reasonable hand-written literal, into a float.
 * Uses Ryu (s2f) for correctly rounded parsing; no newlib strtod, no libm, no heap.
 *
 * Accepts an optional leading '+'/'-', "inf"/"infinity"/"nan" case-insensitively, decimal
 * literals with an optional fractional part (either side of the '.' may be empty, so ".5" and
 * "5." both parse), an optional exponent ('e'/'E' with an optional sign), and an optional
 * trailing 'f'/'F' suffix. Rejects anything else, including trailing garbage.
 *
 * Ryu's s2f rejects mantissas with 9 or more significant digits outright, so literals with more
 * than 9 significant digits are first rounded to 9 (round-half-to-even on the 10th digit, with a
 * sticky bit for anything beyond). Every string formatFloat() produces has at most 9 significant
 * digits and therefore always round-trips exactly; a rare hand-written literal with more digits
 * than that may be off by 1 ULP.
 *
 * @param text the text to parse.
 * @return the parsed value, or DecodeError::MalformedInput / DecodeError::NumberTooBig.
 */
std::expected<float, DecodeError> parseFloat(std::string_view text);

} // namespace utils::FloatText

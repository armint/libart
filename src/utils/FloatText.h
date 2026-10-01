#pragma once
#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include <cstddef>
#include <limits>
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
 * exponent otherwise), plus explicit "inf", "-inf" and "nan". Uses Ryu (f2d, the same decimal
 * core f2s_buffered_n() uses) for a correctly rounded, shortest round-trip decimal mantissa and
 * exponent, formatted directly with no intermediate text and no division; no newlib printf, no
 * libm, no heap.
 *
 * The output never has a trailing '\0'; the caller decides whether it needs one.
 *
 * @param value the float to format.
 * @param dest the buffer to write into. kMaxTextLength bytes is always enough.
 * @return the number of characters written, or 0 if dest is too small.
 */
size_t formatFloat(float value, art::span<char> dest);

/**
 * Parses text produced by formatFloat(), or a reasonable hand-written literal, into a float in a
 * single left-to-right pass. Uses Ryu (s2f) for the actual decimal-to-binary conversion; no
 * newlib strtod, no libm, no heap.
 *
 * Accepts an optional leading '+'/'-', "inf"/"infinity"/"nan" case-insensitively, decimal
 * literals with an optional fractional part (either side of the '.' may be empty, so ".5" and
 * "5." both parse), an optional exponent ('e'/'E' with an optional sign, of any magnitude - an
 * exponent that would over/underflow float saturates to +-inf/0 rather than erroring), and an
 * optional trailing 'f'/'F' suffix. There is no limit on the literal's length. Rejects anything
 * else, including trailing garbage; currently the only error this returns is
 * DecodeError::MalformedInput.
 *
 * Ryu's s2f rejects mantissas with 10 or more significant digits outright, so a literal with more
 * than 9 significant digits is first rounded to 9 (round-half-to-even on the 10th digit, with a
 * sticky bit for the rest), and that rounded decimal is then handed to s2f for the
 * decimal-to-binary conversion. Every string formatFloat() produces has at most 9 significant
 * digits and therefore always round-trips exactly. A hand-written literal with more digits than
 * that goes through two roundings (decimal-to-9-digits, then decimal-to-binary) instead of one
 * correctly-rounded conversion, so it can be off by 1 ULP from the true correctly-rounded result:
 * measured against glibc strtof on 10-, 12- and 17-significant-digit random literals, about
 * 1.27% come out 1 ULP off (never more), almost entirely cases within 1 ULP of the exact halfway
 * point between two representable floats. This does not affect protobuf text format in practice:
 * both protoc and Python's text_format print float fields with at most 9 significant digits, so
 * their output always round-trips exactly through this function.
 *
 * @param text the text to parse.
 * @return the parsed value, or a DecodeError.
 */
art::expected<float, DecodeError> parseFloat(std::string_view text);

} // namespace utils::FloatText

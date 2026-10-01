
#pragma once
#include <cstring>

#include "ArrayReadable.h"

/**
 * @class StringReadable
 * @brief A utility class designed for reading and processing text from strings.
 *
 * The StringReadable class provides methods for handling and manipulating
 * string input for various text processing activities. It integrates
 * functionality to make accessing and consuming string data efficient.
 *
 * This class is especially useful for scenarios where operations similar to
 * file reading are desired, but the data comes from a string source instead.
 */
class StringReadable : public ArrayReadable {
   public:
   explicit StringReadable(const char *str)
      : ArrayReadable(art::span(reinterpret_cast<const uint8_t *>(str), strlen(str))) {
   }

   StringReadable(StringReadable &&) = default;
   StringReadable &operator=(StringReadable &&) = default;
   StringReadable(const StringReadable &) = default;
   StringReadable &operator=(const StringReadable &) = default;
};


#include "JSONWriter.h"

size_t JSONWriter::write(const uint8_t *buffer, size_t length) {
   auto res = _writable.write(std::span<const uint8_t>(buffer, length));
   return res.has_value() ? res.value() : 0;
}

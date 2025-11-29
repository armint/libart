#include "NullWritable.h"

std::expected<size_t, Writable::Error> NullWritable::write(std::span<const uint8_t> data) {
   return data.size();
}
std::expected<void, Writable::Error> NullWritable::flush() {
   return {};
}
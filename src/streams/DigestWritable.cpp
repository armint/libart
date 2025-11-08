
#include "DigestWritable.h"

std::expected<size_t, Writable::Error> DigestWritable::write(std::span<const uint8_t> data) {
   auto res = sink_.write(data);
   if(res) {
      digest_.update(data);
   }
   return res;
}

std::expected<void, Writable::Error> DigestWritable::flush() {
   return sink_.flush();
}
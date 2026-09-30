
#include "DigestWritable.h"

art::expected<size_t, Writable::Error> DigestWritable::write(art::span<const uint8_t> data) {
   auto res = sink_.write(data);
   if(res) {
      digest_.update(data);
   }
   return res;
}

art::expected<void, Writable::Error> DigestWritable::flush() {
   return sink_.flush();
}

#include "CountingWritableWrapper.h"

std::expected<size_t, Writable::Error> CountingWritableWrapper::write(std::span<const uint8_t> data) {
   auto res = _sink.write(data);
   if(res) {
      _written += res.value();
   }
   return res;
}

std::expected<void, Writable::Error> CountingWritableWrapper::flush() {
   return _sink.flush();
}

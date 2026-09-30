
#include "CountingWritableWrapper.h"

art::expected<size_t, Writable::Error> CountingWritableWrapper::write(art::span<const uint8_t> data) {
   auto res = _sink.write(data);
   if(res) {
      _written += res.value();
   }
   return res;
}

art::expected<void, Writable::Error> CountingWritableWrapper::flush() {
   return _sink.flush();
}

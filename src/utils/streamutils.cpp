
#include "streamutils.h"
#include <streams/CountingWritableWrapper.h>

namespace utils {
art::expected<size_t, Readable::Error> copyStream(Readable &from, Writable &to, size_t maxCount) {
   CountingWritableWrapper countingWritableWrapper(to);
   for(size_t i = 0; i < maxCount && from.available(); i++) {
      auto r = from.read();
      if(!r) {
         return art::unexpected(r.error());
      }
      auto wr = countingWritableWrapper.write(art::span<const uint8_t>(reinterpret_cast<uint8_t *>(&r.value()), 1));
      if(!wr) {
         // Error on write
         break;
      }
   }
   return countingWritableWrapper.written();
}
} // namespace utils
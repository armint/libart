#include "AsymmetricKeyWritable.h"

art::expected<size_t, Writable::Error> AsymmetricKeyWritable::write(art::span<const uint8_t> data) {
   auto res = sink_.write(data);
   if(res) {
      auto keyRes = key_.write(data);
      if(!keyRes) {
         return art::unexpected(keyRes.error());
      }
   }
   return res;
}

art::expected<void, Writable::Error> AsymmetricKeyWritable::flush() {
   auto res = sink_.flush();
   if(res) {
      return key_.flush();
   }
   return res;
}

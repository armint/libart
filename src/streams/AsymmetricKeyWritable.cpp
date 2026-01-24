#include "AsymmetricKeyWritable.h"

std::expected<size_t, Writable::Error> AsymmetricKeyWritable::write(std::span<const uint8_t> data) {
   auto res = sink_.write(data);
   if(res) {
      auto keyRes = key_.write(data);
      if(!keyRes) {
         return std::unexpected(keyRes.error());
      }
   }
   return res;
}

std::expected<void, Writable::Error> AsymmetricKeyWritable::flush() {
   auto res = sink_.flush();
   if(res) {
      return key_.flush();
   }
   return res;
}

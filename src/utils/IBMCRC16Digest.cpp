
#include "IBMCRC16Digest.h"

void IBMCRC16Digest::update(art::span<const uint8_t> data) {
   for(auto b : data) {
      digest_ ^= b;
      for(int i = 0; i < 8; ++i) {
         if(digest_ & 1) {
            digest_ = (digest_ >> 1) ^ polynomial_;
         } else {
            digest_ = digest_ >> 1;
         }
      }
   }
}
art::expected<size_t, Digest::Error> IBMCRC16Digest::getDigest(art::span<uint8_t> target) {
   if(target.size() < sizeof(digest_)) {
      return art::unexpected(ArrayTooSmall);
   }
   auto p = target.data();
   *reinterpret_cast<uint16_t *>(p) = digest_;
   return sizeof(digest_);
}

void IBMCRC16Digest::reset() {
   digest_ = initial_value_;
}
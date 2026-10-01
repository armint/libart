
#include "PseudoRandomReadable.h"

PseudoRandomReadable::PseudoRandomReadable(uint64_t seed)
   : rng_(seed, 1) {
}

art::expected<char, Readable::Error> PseudoRandomReadable::read() {
   if(idx_ == 0) {
      last_ = rng_.next();
      idx_ = sizeof(last_);
   }
   uint8_t val = static_cast<uint8_t>(last_ & 0xFF);
   last_ >>= 8;
   --idx_;
   return val;
}

bool PseudoRandomReadable::available() {
   return true;
}
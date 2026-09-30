#include "AsymmetricKeyReadable.h"

art::expected<char, Readable::Error> AsymmetricKeyReadable::read() {
   auto ch = source_.read();
   if(ch) {
      auto res = key_.write(static_cast<uint8_t>(*ch));
      if(!res) {
         // Map Writable::Error to Readable::Error?
         // For now, just return ReadError.
         return art::unexpected(Readable::ReadError);
      }
   }
   return ch;
}

bool AsymmetricKeyReadable::available() {
   return source_.available();
}

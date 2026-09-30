
#include "DigestReadable.h"

DigestReadable::DigestReadable(Readable &source, Digest &digest)
   : source_(source)
   , digest_(digest) {}
art::expected<char, Readable::Error> DigestReadable::read() {
   auto res = source_.read();
   if(res) {
      digest_.update(res.value());
   }
   return res;
}
bool DigestReadable::available() {
   return source_.available();
}
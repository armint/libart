#include "HexReadable.h"

#include <cctype>

std::expected<char, Readable::Error> HexReadable::read() {
   auto res = source_.read();
   auto strchr1 = strchr(digits, tolower(res.value()));
   if(!strchr1) {
      return std::unexpected(InvalidCharacter);
   }
   res = source_.read();
   if(!res) {
      return res;
   }
   auto strchr2 = strchr(digits, tolower(res.value()));
   if(!strchr2) {
      return std::unexpected(InvalidCharacter);
   }

   auto val = (strchr1 - digits) * 16 + (strchr2 - digits);
   return static_cast<char>(val);
}

bool HexReadable::available() {
   // TODO this is not strictly correct, but we need to change the method signature of available() to make it work properly
   return source_.available();
}
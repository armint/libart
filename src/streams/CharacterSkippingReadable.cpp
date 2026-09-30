#include <cstring>

#include "CharacterSkippingReadable.h"

CharacterSkippingReadable::CharacterSkippingReadable(Readable &source, const char *charactersToSkip)
   : source(source)
   , charactersToSkip(charactersToSkip) {}

art::expected<char, Readable::Error> CharacterSkippingReadable::read() {
   art::expected<char, Readable::Error> r;
   if(nextCharacter) {
      r = nextCharacter.value();
      nextCharacter.reset();
   } else {
      do {
         r = source.read();
      } while(r && strchr(charactersToSkip, r.value()) != nullptr);
   }
   return r;
}

bool CharacterSkippingReadable::available() {
   if(nextCharacter) {
      return true;
   }
   auto result = read();
   if(!result) {
      return false;
   }
   nextCharacter.emplace(result.value());
   return true;
}

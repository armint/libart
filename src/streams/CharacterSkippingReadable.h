
#pragma once

#include "Readable.h"

/**
 * A readable that will skip any character found in the provided string
 */
class CharacterSkippingReadable : public Readable {
   Readable &source;
   const char *charactersToSkip;
   std::optional<char> nextCharacter;

   public:
   CharacterSkippingReadable(Readable &source, const char *charactersToSkip);
   virtual std::expected<char, Error> read() override;
   virtual bool available() override;
};

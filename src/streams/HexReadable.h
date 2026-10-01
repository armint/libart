#pragma once
#include "Readable.h"


class HexReadable : public Readable {
   Readable &source_;
   static constexpr const char *digits = "0123456789abcdef";

   public:
   enum { InvalidCharacter = 10 };
   explicit HexReadable(Readable &source)
      : source_(source) {
   }

   HexReadable(HexReadable &&) = default;
   HexReadable &operator=(HexReadable &&) = delete;
   HexReadable(const HexReadable &) = delete;
   HexReadable &operator=(const HexReadable &) = delete;

   art::expected<char, Error> read() override;
   bool available() override;
};
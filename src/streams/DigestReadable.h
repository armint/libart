
#pragma once
#include "Readable.h"
#include "utils/Digest.h"

class DigestReadable : public Readable {
   protected:
   Readable &source_;
   Digest &digest_;

   public:
   DigestReadable(Readable &source, Digest &digest);

   DigestReadable(DigestReadable&&) = default;
   DigestReadable& operator=(DigestReadable&&) = delete;
   DigestReadable(const DigestReadable&) = delete;
   DigestReadable& operator=(const DigestReadable&) = delete;

   using Readable::read;
   art::expected<char, Error> read() override;
   bool available() override;
   [[nodiscard]] Digest &digest() const {
      return digest_;
   }
};
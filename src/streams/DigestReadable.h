
#pragma once
#include "Readable.h"
#include "utils/Digest.h"

class DigestReadable : public Readable {
   protected:
   Readable &source_;
   Digest &digest_;

   public:
   DigestReadable(Readable &source, Digest &digest);
   std::expected<char, Error> read() override;
   bool available() override;
   [[nodiscard]] Digest &digest() const {
      return digest_;
   }
};
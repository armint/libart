
#pragma once
#include "Writable.h"
#include "utils/Digest.h"


class DigestWritable : public Writable {
   protected:
   Digest &digest_;
   Writable &sink_;

   public:
   DigestWritable(Writable &sink, Digest &digest)
      : digest_(digest)
      , sink_(sink) {}

   using Writable::write;
   std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
   std::expected<void, Error> flush() override;

};

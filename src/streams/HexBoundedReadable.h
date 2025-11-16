#pragma once

#include "BoundedReadable.h"
#include "HexReadable.h"

/**
 * A BoundedReadable that decodes a hexadecimal ASCII stream into raw bytes.
 *
 * It wraps a BoundedReadable source that provides hex characters, and exposes
 * byte-wise reads while preserving bounded semantics (bytesLeft/position/reset).
 */
class HexBoundedReadable : public BoundedReadable {
   BoundedReadable &source_;
   HexReadable hex_;

   public:
   enum { InvalidCharacter = HexReadable::InvalidCharacter };

   explicit HexBoundedReadable(BoundedReadable &source)
      : source_(source), hex_(source) {}

   // Readable
   std::expected<char, Error> read() override;
   bool available() override;

   // BoundedReadable
   size_t bytesLeft() override;
   void reset(size_t position = 0) override;
   size_t position() override;
};

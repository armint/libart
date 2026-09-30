#pragma once

#include "HexReadable.h"
#include "RandomAccessReadable.h"

/**
 * A RandomAccessReadable that decodes a hexadecimal ASCII stream into raw bytes.
 *
 * It wraps a RandomAccessReadable source that provides hex characters, and exposes
 * byte-wise reads while preserving bounded semantics (bytesLeft/position/reset).
 */
class HexBoundedReadable : public RandomAccessReadable {
   RandomAccessReadable &source_;
   HexReadable hex_;

   public:
   enum { InvalidCharacter = HexReadable::InvalidCharacter };

   explicit HexBoundedReadable(RandomAccessReadable &source)
      : source_(source)
      , hex_(source) {}

   HexBoundedReadable(HexBoundedReadable&&) = default;
   HexBoundedReadable& operator=(HexBoundedReadable&&) = delete;
   HexBoundedReadable(const HexBoundedReadable&) = delete;
   HexBoundedReadable& operator=(const HexBoundedReadable&) = delete;

   // Readable
   art::expected<char, Error> read() override;
   bool available() override;

   // BoundedReadable
   size_t bytesLeft() override;
   size_t position() override;
   // RandomAccessReadable
   void reset(size_t position = 0) override;
};

#pragma once
#include "BoundedReadable.h"

/**
 * A readable reading from a memory array
 */
class ArrayReadable : public BoundedReadable {
   size_t index = 0;
   std::span<const uint8_t> data;

   public:
   ArrayReadable(std::span<const uint8_t> data);
   virtual std::expected<char, Error> read() override;

   virtual bool available() override;

   virtual size_t bytesLeft() override;
   virtual void reset(size_t position = 0) override;
   virtual size_t position() override;
};

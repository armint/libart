
#pragma once
#include "BoundedWritable.h"
#include <span>

/**
 * A writable writing to a memory array
 */
class ArrayWritable : public BoundedWritable {
   std::span<uint8_t> _buffer;
   size_t _position = 0;

   public:
   ArrayWritable(std::span<uint8_t> buffer)
      : BoundedWritable(buffer.size())
      , _buffer(buffer) {
   }
   virtual std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
   virtual std::expected<void, Error> flush() override;
   virtual void reset(size_t position = 0) override;
   virtual size_t position() override;
};

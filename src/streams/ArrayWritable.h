
#pragma once
#include "BoundedWritable.h"
#include "utils/compat/span.h"

/**
 * A writable writing to a memory array
 */
class ArrayWritable : public BoundedWritable {
   protected:
   art::span<uint8_t> _buffer;
   size_t _position = 0;

   public:
   ArrayWritable(art::span<uint8_t> buffer)
      : BoundedWritable(buffer.size())
      , _buffer(buffer) {}

   ArrayWritable(ArrayWritable&&) = default;
   ArrayWritable& operator=(ArrayWritable&&) = default;
   ArrayWritable(const ArrayWritable&) = default;
   ArrayWritable& operator=(const ArrayWritable&) = default;

   virtual art::expected<size_t, Error> write(art::span<const uint8_t> data) override;
   virtual art::expected<void, Error> flush() override;
   virtual void reset(size_t position = 0) override;
   virtual size_t position() override;
};

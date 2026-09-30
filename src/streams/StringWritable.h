#pragma once

#include "ArrayWritable.h"

class StringWritable : public ArrayWritable {
   public:
   explicit StringWritable(art::span<char> buffer)
      : ArrayWritable(art::span(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size())) {}

   StringWritable(StringWritable&&) = default;
   StringWritable& operator=(StringWritable&&) = default;
   StringWritable(const StringWritable&) = default;
   StringWritable& operator=(const StringWritable&) = default;

   art::expected<size_t, Error> write(art::span<const uint8_t> data) override {
      auto res = ArrayWritable::write(data);
      // We need to make sure the string is always zero-terminated
      if (_position == _buffer.size()) {
         _buffer[_buffer.size() - 1] = 0;
      }
      return res;
   }
};
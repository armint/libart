#pragma once

#include "ArrayWritable.h"

class StringWritable : public ArrayWritable {
   public:
   explicit StringWritable(std::span<char> buffer)
      : ArrayWritable(std::span(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size())) {}

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override {
      auto res = ArrayWritable::write(data);
      // We need to make sure the string is always zero-terminated
      if (_position == _buffer.size()) {
         _buffer[_buffer.size() - 1] = 0;
      }
      return res;
   }
};
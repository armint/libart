#pragma once
#include "RandomAccessReadable.h"

/**
 * A readable reading from a memory array
 */
class ArrayReadable : public RandomAccessReadable {
   size_t index = 0;
   std::span<const uint8_t> data;

   public:
   explicit ArrayReadable(std::span<const uint8_t> data)
      : data(data) {}

   ArrayReadable(ArrayReadable&&) = default;
   ArrayReadable& operator=(ArrayReadable&&) = default;
   ArrayReadable(const ArrayReadable&) = default;
   ArrayReadable& operator=(const ArrayReadable&) = default;

   std::expected<char, Error> read() override {
      if(index >= data.size()) {
         return std::unexpected(EndOfStream);
      }
      return data[index++];
   }

   bool available() override { return index < data.size(); }

   size_t bytesLeft() override { return data.size() - index; }

   void reset(size_t position = 0) override {
      if(position > data.size()) {
         position = data.size();
      }
      index = position;
   }

   size_t position() override { return index; }
};
#pragma once
#include "RandomAccessReadable.h"

/**
 * A readable reading from a memory array
 */
class ArrayReadable : public RandomAccessReadable {
   size_t index = 0;
   std::span<const uint8_t> data;

   public:
   ArrayReadable(std::span<const uint8_t> data);
   std::expected<char, Error> read() override;

   bool available() override;

   size_t bytesLeft() override;
   void reset(size_t position = 0) override;
   size_t position() override;
};
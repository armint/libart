#pragma once
#include "CountingWritable.h"


class NullWritable : public CountingWritable {
   size_t _written = 0;

   public:
   NullWritable() = default;
   NullWritable(NullWritable&&) = default;
   NullWritable& operator=(NullWritable&&) = default;
   NullWritable(const NullWritable&) = default;
   NullWritable& operator=(const NullWritable&) = default;

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override;

   size_t written() override {
      return _written;
   }

   virtual std::expected<void, Error> flush() override;
};

#pragma once
#include "CountingWritable.h"

/**
 * A writable that wraps another writable and counts the characters written
 */
class CountingWritableWrapper : public CountingWritable {
   Writable &_sink;
   size_t _written = 0;

   public:
   CountingWritableWrapper(Writable &writable)
      : _sink(writable) {}

   CountingWritableWrapper(CountingWritableWrapper&&) = default;
   CountingWritableWrapper& operator=(CountingWritableWrapper&&) = delete;
   CountingWritableWrapper(const CountingWritableWrapper&) = delete;
   CountingWritableWrapper& operator=(const CountingWritableWrapper&) = delete;

   virtual art::expected<size_t, Error> write(art::span<const uint8_t> data) override;

   virtual size_t written() override {
      return _written;
   }

   virtual art::expected<void, Error> flush() override;
};

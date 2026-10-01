
#pragma once
#include "Writable.h"

/**
 * A writable that maintains a buffer, and will only write to the sink when the buffer is full, or when flush is called.
 * Once the buffer is full (or flush is called) ALL the available data will be written to the sink.
 * Useful for writing to block devices such as FLASH storage or for packet based communication
 */
class BufferedWritable : public Writable {
   Writable &_sink;
   art::span<uint8_t> _buffer;
   size_t _index = 0;

   public:
   BufferedWritable(Writable &sink, art::span<uint8_t> buffer);

   BufferedWritable(BufferedWritable &&) = default;
   BufferedWritable &operator=(BufferedWritable &&) = delete;
   BufferedWritable(const BufferedWritable &) = delete;
   BufferedWritable &operator=(const BufferedWritable &) = delete;

   art::expected<size_t, Error> write(art::span<const uint8_t> data) override;
   art::expected<void, Error> flush() override;
   size_t getBufferIndex() const {
      return _index;
   }
};

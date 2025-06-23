
#pragma once
#include "Writable.h"
#include <cstring>
/**
 * A writable that maintains a buffer, and will only write to the sink when the buffer is full, or when flush is called.
 * Once the buffer is full (or flush is called) ALL the available data will be written to the sink.
 * Useful for writing to block devices such as FLASH storage or for packet based communication
 */
template <typename T>
class BufferedWritable : public Writable {
   T &_sink;
   std::span<uint8_t> _buffer;
   size_t _index = 0;

   public:
   BufferedWritable(T &sink, std::span<uint8_t> buffer);
   virtual std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
   virtual std::expected<void, Error> flush() override;
   size_t getBufferIndex() const {
      return _index;
   }
};

template <typename T>
BufferedWritable<T>::BufferedWritable(T &sink, std::span<uint8_t> buffer)
   : _sink(sink)
   , _buffer(buffer) {
}

template <typename T>
std::expected<size_t, Writable::Error> BufferedWritable<T>::write(std::span<const uint8_t> data) {
   size_t writeCount = data.size();
   size_t dataIndex = 0;
   do {
      size_t writeNow = _buffer.size() - _index;
      if(writeNow > writeCount) {
         writeNow = writeCount;
      }
      memcpy(_buffer.data() + _index, data.data() + dataIndex, writeNow);
      writeCount -= writeNow;
      _index += writeNow;
      dataIndex += writeNow;
      if(_index == _buffer.size()) {
         auto result = _sink.write(_buffer);
         if(!result) {
            return std::unexpected(result.error());
         }
         _index = 0;
      }
   } while(writeCount > 0);
   return data.size();
}

template <typename T>
std::expected<void, Writable::Error> BufferedWritable<T>::flush() {
   if(_index > 0) {
      auto result = _sink.write(_buffer.subspan(0, _index));
      if(!result) {
         return std::unexpected(result.error());
      }
      _index = 0;
   }
   return _sink.flush();
}

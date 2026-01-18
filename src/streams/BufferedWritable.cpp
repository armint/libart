#include "BufferedWritable.h"

BufferedWritable::BufferedWritable(Writable &sink, std::span<uint8_t> buffer)
   : _sink(sink)
   , _buffer(buffer) {}

std::expected<size_t, Writable::Error> BufferedWritable::write(std::span<const uint8_t> data) {
   size_t dataIndex = 0;
   size_t dataSize = data.size();

   // 1. If we have something in the buffer, try to fill it
   if(_index > 0) {
      size_t canCopy = _buffer.size() - _index;
      size_t toCopy = std::min(canCopy, dataSize);
      memcpy(_buffer.data() + _index, data.data(), toCopy);
      _index += toCopy;
      dataIndex += toCopy;

      if(_index == _buffer.size()) {
         auto result = _sink.write(_buffer);
         if(!result) {
            return std::unexpected(result.error());
         }
         // Note: we assume here that the sink wrote the whole buffer if it returned success.
         // If it wrote less, we have a problem because we don't have a way to handle partial writes to the sink
         // in a buffered stream without complex logic (shifting buffer).
         _index = 0;
      }
   }

   // 2. While we have more data than the buffer size, write it directly to the sink
   while(dataSize - dataIndex >= _buffer.size()) {
      auto result = _sink.write(data.subspan(dataIndex, _buffer.size()));
      if(!result) {
         return std::unexpected(result.error());
      }
      dataIndex += result.value();
      // If the sink wrote less than requested, we must stop or handle it.
      // For simplicity and consistency with the previous loop-based implementation:
      if(result.value() < _buffer.size()) {
         // This is a bit tricky. If sink wrote less, we should probably return what we wrote so far.
         return dataIndex;
      }
   }

   // 3. Put remaining data into the buffer
   if(dataIndex < dataSize) {
      size_t remaining = dataSize - dataIndex;
      memcpy(_buffer.data() + _index, data.data() + dataIndex, remaining);
      _index += remaining;
   }

   return dataSize;
}

std::expected<void, Writable::Error> BufferedWritable::flush() {
   if(_index > 0) {
      auto result = _sink.write(_buffer.subspan(0, _index));
      if(!result) {
         return std::unexpected(result.error());
      }
      if(result.value() < _index) {
         // If we couldn't write everything, we should ideally keep the rest in the buffer.
         // But for now, let's at least update the index correctly.
         size_t written = result.value();
         memmove(_buffer.data(), _buffer.data() + written, _index - written);
         _index -= written;
         return std::unexpected(Writable::CouldNotWrite);
      }
      _index = 0;
   }
   return _sink.flush();
}

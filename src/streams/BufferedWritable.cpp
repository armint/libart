#include "BufferedWritable.h"

BufferedWritable::BufferedWritable(Writable &sink, std::span<uint8_t> buffer)
   : _sink(sink)
   , _buffer(buffer) {
}

std::expected<size_t, Writable::Error> BufferedWritable::write(std::span<const uint8_t> data) {
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

std::expected<void, Writable::Error> BufferedWritable::flush() {
   if(_index > 0) {
      auto result = _sink.write(_buffer.subspan(0, _index));
      if(!result) {
         return std::unexpected(result.error());
      }
      _index = 0;
   }
   return _sink.flush();
}


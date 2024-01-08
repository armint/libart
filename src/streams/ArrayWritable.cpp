
#include "ArrayWritable.h"

std::expected<size_t, Writable::Error> ArrayWritable::write(std::span<const uint8_t> data) {
   size_t index;
   for(index = 0; index < data.size() && _position < _buffer.size(); index++, _position++) {
      _buffer[_position] = data[index];
   }
   if(index != data.size()) {
      return std::unexpected(Overflow);
   }
   return index;
}

std::expected<void, Writable::Error> ArrayWritable::flush() {
   // Nothing to do here
   return {};
}

void ArrayWritable::reset(size_t position) {
   _position = position;
}

size_t ArrayWritable::position() {
   return _position;
}

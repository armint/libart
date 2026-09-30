
#include "ArrayWritable.h"

art::expected<size_t, Writable::Error> ArrayWritable::write(art::span<const uint8_t> data) {
   size_t index;
   for(index = 0; index < data.size() && _position < _buffer.size(); index++, _position++) {
      _buffer[_position] = data[index];
   }
   if(index != data.size()) {
      return art::unexpected(Overflow);
   }
   return index;
}

art::expected<void, Writable::Error> ArrayWritable::flush() {
   // Nothing to do here
   return {};
}

void ArrayWritable::reset(size_t position) {
   _position = position;
}

size_t ArrayWritable::position() {
   return _position;
}

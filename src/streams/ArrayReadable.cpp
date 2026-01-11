
#include "ArrayReadable.h"
ArrayReadable::ArrayReadable(std::span<const uint8_t> data)
   : data(data) {}

std::expected<char, Readable::Error> ArrayReadable::read() {
   if(index >= data.size()) {
      return std::unexpected(EndOfStream);
   }
   return data[index++];
}

size_t ArrayReadable::bytesLeft() {
   return data.size() - index;
}

void ArrayReadable::reset(size_t position) {
   if(position > data.size()) {
      position = data.size();
   }
   index = position;
}

size_t ArrayReadable::position() {
   return index;
}

bool ArrayReadable::available() {
   return bytesLeft() > 0;
}

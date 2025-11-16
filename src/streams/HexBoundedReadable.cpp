#include "HexBoundedReadable.h"

std::expected<char, Readable::Error> HexBoundedReadable::read() {
   return hex_.read();
}

bool HexBoundedReadable::available() {
   return hex_.available();
}

size_t HexBoundedReadable::bytesLeft() {
   return source_.bytesLeft() / 2;
}

void HexBoundedReadable::reset(size_t position) {
   source_.reset(position * 2);
}

size_t HexBoundedReadable::position() {
   return source_.position() / 2;
}

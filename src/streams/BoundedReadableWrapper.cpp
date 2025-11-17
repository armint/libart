#include "BoundedReadableWrapper.h"

std::expected<char, Readable::Error> BoundedReadableWrapper::read() {
   if(position_ >= limit_) {
      return std::unexpected(EndOfStream);
   }
   return source_.read();
}

bool BoundedReadableWrapper::available() {
   if(position_ >= limit_) {
      return false;
   }
   return source_.available();
}

size_t BoundedReadableWrapper::bytesLeft() {
   return limit_ - position_;
}

size_t BoundedReadableWrapper::position() {
   return position_;
}
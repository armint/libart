#include "BoundedReadableWrapper.h"

art::expected<char, Readable::Error> BoundedReadableWrapper::read() {
   if(position_ >= limit_) {
      return art::unexpected(EndOfStream);
   }
   auto ch = source_.read();
   if(ch) {
      ++position_;
   }
   return ch;
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
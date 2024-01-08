
#include "JSONReader.h"

int JSONReader::read() {
   auto r = _source.read();
   return r.has_value() ? r.value() : -1;
}

size_t JSONReader::readBytes(char *buffer, size_t length) {
   size_t i;
   for(i = 0; i < length; i++) {
      auto r = _source.read();
      if(!r.has_value()) {
         break;
      }
      buffer[i] = r.value();
   }
   return i;
}

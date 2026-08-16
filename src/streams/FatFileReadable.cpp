
#include "FatFileReadable.h"

std::expected<char, Readable::Error> FatFileReadable::read() {
   if(bufferIndex >= bytesBuffered) {
      if(_buffer.empty()) {
         return std::unexpected(ReadError);
      }
      if(file.tell() >= file.size()) {
         return std::unexpected(EndOfStream);
      }
      bufferIndex = 0;
      auto result = file.read(_buffer);
      if(!result) {
         _lastError = result.error();
         return std::unexpected(ReadError);
      }
      bytesBuffered = result.value();
      if(bytesBuffered == 0) {
         return std::unexpected(EndOfStream);
      }
   }
   return static_cast<char>(_buffer[bufferIndex++]);
}

void FatFileReadable::reset(size_t position) {
   size_t startOfBufferOffset = file.tell() - bytesBuffered;
   if(bytesBuffered > 0 && position >= startOfBufferOffset && position < startOfBufferOffset + bytesBuffered) {
      bufferIndex = position - startOfBufferOffset;
      return;
   }
   auto result = file.seek(position);
   if(!result) {
      _lastError = result.error();
   }
   bufferIndex = 0;
   bytesBuffered = 0;
}

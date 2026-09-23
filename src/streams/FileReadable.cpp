#include "FileReadable.h"
#include <algorithm>
#include <limits>

namespace {

// A stream reports sizes as size_t, which is narrower than a file offset on a 32-bit target.
size_t saturatingSize(art::storage::offset_t value) {
   return static_cast<size_t>(std::min<art::storage::offset_t>(value, std::numeric_limits<size_t>::max()));
}

} // namespace

std::expected<char, Readable::Error> FileReadable::read() {
   if(bufferIndex_ >= bytesBuffered_) {
      bufferIndex_ = 0;
      bytesBuffered_ = 0;
      if(buffer_.empty()) {
         return std::unexpected(ReadError);
      }
      // File::read() returns 0 at the end of the file, so no size() call is needed to detect it.
      const auto result = file_.read(buffer_);
      if(!result) {
         lastError_ = result.error();
         return std::unexpected(toReadableError(result.error()));
      }
      if(*result == 0) {
         return std::unexpected(EndOfStream);
      }
      bytesBuffered_ = *result;
   }
   return static_cast<char>(buffer_[bufferIndex_++]);
}

bool FileReadable::available() {
   if(bufferIndex_ < bytesBuffered_) {
      return true;
   }
   const auto position = file_.tell();
   const auto size = file_.size();
   return position && size && *position < *size;
}

size_t FileReadable::bytesLeft() {
   size_t left = bytesBuffered_ - bufferIndex_;
   const auto position = file_.tell();
   const auto size = file_.size();
   if(position && size && *size > *position) {
      left += saturatingSize(*size - *position);
   }
   return left;
}

void FileReadable::reset(size_t position) {
   const auto filePosition = file_.tell();
   if(!filePosition) {
      lastError_ = filePosition.error();
      return;
   }
   const art::storage::offset_t bufferStart = *filePosition - bytesBuffered_;
   if(bytesBuffered_ > 0 && position >= bufferStart && position < bufferStart + bytesBuffered_) {
      bufferIndex_ = static_cast<size_t>(position - bufferStart);
      return;
   }
   const auto result = file_.seek(position);
   if(!result) {
      lastError_ = result.error();
      return;
   }
   bufferIndex_ = 0;
   bytesBuffered_ = 0;
}

size_t FileReadable::position() {
   const auto filePosition = file_.tell();
   if(!filePosition) {
      lastError_ = filePosition.error();
      return 0;
   }
   return saturatingSize(*filePosition - bytesBuffered_ + bufferIndex_);
}

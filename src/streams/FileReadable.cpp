#include "FileReadable.h"
#include <algorithm>
#include <limits>

namespace {

// A stream reports sizes as size_t, which is narrower than a file offset on a 32-bit target.
size_t saturatingSize(art::storage::offset_t value) {
   return static_cast<size_t>(std::min<art::storage::offset_t>(value, std::numeric_limits<size_t>::max()));
}

} // namespace

art::expected<char, Readable::Error> FileReadable::read() {
   if(bufferIndex_ >= bytesBuffered_) {
      bufferIndex_ = 0;
      bytesBuffered_ = 0;
      if(buffer_.empty()) {
         return art::unexpected(ReadError);
      }
      // File::read() returns 0 at the end of the file, so no size() call is needed to detect it.
      const auto result = file_.read(buffer_);
      if(!result) {
         lastError_ = result.error();
         return art::unexpected(toReadableError(result.error()));
      }
      if(*result == 0) {
         return art::unexpected(EndOfStream);
      }
      bytesBuffered_ = *result;
   }
   return static_cast<char>(buffer_[bufferIndex_++]);
}

bool FileReadable::fileExtent(art::storage::offset_t &position, art::storage::offset_t &size) {
   const auto filePosition = file_.tell();
   if(!filePosition) {
      lastError_ = filePosition.error();
      return false;
   }
   const auto fileSize = file_.size();
   if(!fileSize) {
      lastError_ = fileSize.error();
      return false;
   }
   position = *filePosition;
   size = *fileSize;
   return true;
}

art::storage::offset_t FileReadable::bufferStart(art::storage::offset_t filePosition) const {
   // The file position is at least the buffered byte count unless the file was moved behind the stream's back.
   return filePosition > bytesBuffered_ ? filePosition - bytesBuffered_ : 0;
}

bool FileReadable::available() {
   if(bufferIndex_ < bytesBuffered_) {
      return true;
   }
   art::storage::offset_t position = 0;
   art::storage::offset_t size = 0;
   return fileExtent(position, size) && position < size;
}

size_t FileReadable::bytesLeft() {
   // Unlike available(), this needs the file size even when the buffer holds unread bytes.
   size_t left = bytesBuffered_ - bufferIndex_;
   art::storage::offset_t position = 0;
   art::storage::offset_t size = 0;
   if(fileExtent(position, size) && size > position) {
      left += saturatingSize(size - position);
   }
   return left;
}

void FileReadable::reset(size_t position) {
   const auto filePosition = file_.tell();
   if(!filePosition) {
      lastError_ = filePosition.error();
      return;
   }
   const art::storage::offset_t start = bufferStart(*filePosition);
   if(bytesBuffered_ > 0 && position >= start && position < start + bytesBuffered_) {
      bufferIndex_ = static_cast<size_t>(position - start);
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
   return saturatingSize(bufferStart(*filePosition) + bufferIndex_);
}

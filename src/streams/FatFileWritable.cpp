
#include "FatFileWritable.h"
#include <cstring>

std::expected<size_t, Writable::Error> FatFileWritable::write(std::span<const uint8_t> data) {
   size_t writeCount = data.size();
   size_t dataIndex = 0;
   do {
      size_t writeNow = buffer_.size() - index_;
      if(writeNow > writeCount) {
         writeNow = writeCount;
      }
      memcpy(buffer_.data() + index_, data.data() + dataIndex, writeNow);
      writeCount -= writeNow;
      index_ += writeNow;
      dataIndex += writeNow;
      if(index_ == buffer_.size()) {
         auto result = file_.write(buffer_);
         if(!result) {
            return std::unexpected(static_cast<Writable::Error>(result.error()));
         }
         index_ = 0;
      }
   } while(writeCount > 0);
   return data.size();
}

std::expected<void, Writable::Error> FatFileWritable::flush() {
   if(index_ > 0) {
      auto result = file_.write(buffer_.subspan(0, index_));
      if(!result) {
         return std::unexpected(static_cast<Writable::Error>(result.error()));
      }
      index_ = 0;
   }
   auto result = file_.flush();
   if(!result) {
      return std::unexpected(static_cast<Writable::Error>(result.error()));
   }
   return {};
}

void FatFileWritable::reset(size_t position) {
   flush();
   file_.seek(position);
}

size_t FatFileWritable::position() {
   return file_.tell() + index_;
}

FatFileWritable::~FatFileWritable() {
   flush();
}

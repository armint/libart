#include "FileWritable.h"
#include <algorithm>

art::expected<size_t, Writable::Error> FileWritable::write(art::span<const uint8_t> data) {
   const auto result = file_.write(data);
   if(!result) {
      lastError_ = result.error();
      return art::unexpected(toWritableError(result.error()));
   }
   return data.size();
}

art::expected<void, Writable::Error> FileWritable::flush() {
   const auto result = file_.sync();
   if(!result) {
      lastError_ = result.error();
      return art::unexpected(toWritableError(result.error()));
   }
   return {};
}

void FileWritable::reset(size_t position) {
   const auto result = file_.seek(position);
   if(!result) {
      lastError_ = result.error();
   }
}

size_t FileWritable::position() {
   const auto filePosition = file_.tell();
   if(!filePosition) {
      lastError_ = filePosition.error();
      return 0;
   }
   return static_cast<size_t>(std::min<art::storage::offset_t>(*filePosition, std::numeric_limits<size_t>::max()));
}

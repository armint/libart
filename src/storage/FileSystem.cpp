#include "storage/FileSystem.h"

namespace art::storage {

std::expected<size_t, Error> FileSystem::readFile(const char *path, std::span<uint8_t> buffer) {
   auto file = open(path, OpenMode::Read);
   if(!file) {
      return std::unexpected(file.error());
   }
   const auto fileSize = file->size();
   if(!fileSize) {
      return std::unexpected(fileSize.error());
   }
   if(*fileSize > buffer.size()) {
      return std::unexpected(Error::NoMemory);
   }
   const size_t expectedSize = static_cast<size_t>(*fileSize);
   size_t total = 0u;
   while(total < expectedSize) {
      const auto bytesRead = file->read(buffer.subspan(total, expectedSize - total));
      if(!bytesRead) {
         return std::unexpected(bytesRead.error());
      }
      if(*bytesRead == 0u) {
         break;
      }
      total += *bytesRead;
   }
   return total;
}

std::expected<void, Error> FileSystem::writeFile(const char *path, std::span<const uint8_t> data) {
   auto file = open(path, OpenMode::Write);
   if(!file) {
      return std::unexpected(file.error());
   }
   if(!data.empty()) {
      if(const auto written = file->write(data); !written) {
         return written;
      }
   }
   return file->close();
}

} // namespace art::storage

#include "storage/FileSystem.h"

namespace art::storage {

art::expected<size_t, Error> FileSystem::readFile(const char *path, art::span<uint8_t> buffer) {
   auto file = open(path, OpenMode::Read);
   if(!file) {
      return art::unexpected(file.error());
   }
   const auto fileSize = file->size();
   if(!fileSize) {
      return art::unexpected(fileSize.error());
   }
   if(*fileSize > buffer.size()) {
      return art::unexpected(Error::NoMemory);
   }
   const size_t expectedSize = static_cast<size_t>(*fileSize);
   size_t total = 0u;
   while(total < expectedSize) {
      const auto bytesRead = file->read(buffer.subspan(total, expectedSize - total));
      if(!bytesRead) {
         return art::unexpected(bytesRead.error());
      }
      if(*bytesRead == 0u) {
         // The file ended before the size it reported: fail rather than return a silently short count.
         return art::unexpected(Error::Io);
      }
      total += *bytesRead;
   }
   return total;
}

art::expected<void, Error> FileSystem::writeFile(const char *path, art::span<const uint8_t> data) {
   auto file = open(path, OpenMode::Write);
   if(!file) {
      return art::unexpected(file.error());
   }
   if(!data.empty()) {
      if(const auto written = file->write(data); !written) {
         return written;
      }
   }
   return file->close();
}

} // namespace art::storage

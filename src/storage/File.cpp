#include "storage/File.h"
#include "storage/FileSystem.h"

namespace art::storage {

bool File::isOpen() const {
   if(fileSystem_ == nullptr) {
      return false;
   }
   // A live handle answers tell(); only a stale one reports BadHandle.
   const auto position = fileSystem_->fileTell(handle_);
   return position.has_value() || position.error() != Error::BadHandle;
}

std::expected<size_t, Error> File::read(std::span<uint8_t> buffer) {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileRead(handle_, buffer);
}

std::expected<void, Error> File::write(std::span<const uint8_t> data) {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileWrite(handle_, data);
}

std::expected<void, Error> File::seek(offset_t position) {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileSeek(handle_, position);
}

std::expected<offset_t, Error> File::tell() const {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileTell(handle_);
}

std::expected<offset_t, Error> File::size() const {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileSize(handle_);
}

std::expected<void, Error> File::sync() {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileSync(handle_);
}

std::expected<void, Error> File::close() {
   if(fileSystem_ == nullptr) {
      return std::unexpected(Error::BadHandle);
   }
   FileSystem *const fileSystem = fileSystem_;
   fileSystem_ = nullptr;
   return fileSystem->fileClose(handle_);
}

void File::closeSilently() {
   if(fileSystem_ != nullptr) {
      static_cast<void>(fileSystem_->fileClose(handle_));
      fileSystem_ = nullptr;
   }
}

} // namespace art::storage

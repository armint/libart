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

art::expected<size_t, Error> File::read(art::span<uint8_t> buffer) {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileRead(handle_, buffer);
}

art::expected<void, Error> File::write(art::span<const uint8_t> data) {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileWrite(handle_, data);
}

art::expected<void, Error> File::seek(offset_t position) {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileSeek(handle_, position);
}

art::expected<offset_t, Error> File::tell() const {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileTell(handle_);
}

art::expected<offset_t, Error> File::size() const {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileSize(handle_);
}

art::expected<void, Error> File::sync() {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
   }
   return fileSystem_->fileSync(handle_);
}

art::expected<void, Error> File::close() {
   if(fileSystem_ == nullptr) {
      return art::unexpected(Error::BadHandle);
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

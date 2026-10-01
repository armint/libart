#pragma once

#include "storage/FileSystem.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace art::storage::test {

/**
 * @brief FileSystem test double that keeps everything in host memory (it uses the heap; it is test-only).
 *
 * It exists to exercise File and the FileSystem base class without a real backend. It follows the contract of
 * FileSystem.h, including a small handle pool whose handles carry a generation counter, so a slot that was
 * closed and reopened is not mistaken for the old handle.
 *
 * remove() and rename() refuse with Error::Busy while the file involved (source or replaced target) is open,
 * which is one of the two behaviours FileSystem.h allows. Names longer than kMaxNameLength are rejected with
 * Error::NameTooLong.
 *
 * Fault injection: failNextWrite(), failNextClose() and failNextSync() make the next such call fail once.
 * The counters report how often each entry point was reached.
 */
class InMemoryFileSystem final : public FileSystem {
   public:
   static constexpr size_t kMaxOpenFiles = 3u;

   art::expected<void, Error> mount() override {
      mounted_ = true;
      return {};
   }

   art::expected<void, Error> unmount() override {
      for(auto &slot : slots_) {
         releaseSlot(slot);
      }
      mounted_ = false;
      return {};
   }

   art::expected<void, Error> format() override {
      if(mounted_) {
         return art::unexpected(Error::Busy);
      }
      files_.clear();
      directories_.clear();
      return {};
   }

   [[nodiscard]] bool isMounted() const override {
      return mounted_;
   }

   art::expected<File, Error> open(const char *path, OpenMode mode) override {
      ++openCalls_;
      if(!mounted_) {
         return art::unexpected(Error::NotMounted);
      }
      const std::string name{ path };
      if(hasTooLongName(name)) {
         return art::unexpected(Error::NameTooLong);
      }
      if(directories_.contains(name) || name == "/") {
         return art::unexpected(Error::IsDirectory);
      }
      const auto parent = parentOf(name);
      if(!directories_.contains(parent) && parent != "/") {
         return art::unexpected(files_.contains(parent) ? Error::NotDirectory : Error::NoEntry);
      }
      const bool exists = files_.contains(name);
      if(!exists && mode == OpenMode::Read) {
         return art::unexpected(Error::NoEntry);
      }
      if(exists && mode == OpenMode::CreateNew) {
         return art::unexpected(Error::Exists);
      }
      const auto free = std::ranges::find_if(slots_, [](const Slot &slot) { return !slot.used; });
      if(free == slots_.end()) {
         return art::unexpected(Error::NoMemory);
      }
      auto &content = files_[name];
      if(mode == OpenMode::Write || mode == OpenMode::ReadWriteTruncate) {
         content.clear();
      }
      free->used = true;
      free->path = name;
      free->mode = mode;
      free->position = mode == OpenMode::Append ? content.size() : 0u;
      return makeFile(handleOf(static_cast<size_t>(free - slots_.begin()), *free));
   }

   art::expected<void, Error> remove(const char *path) override {
      if(!mounted_) {
         return art::unexpected(Error::NotMounted);
      }
      const std::string name{ path };
      if(isOpenPath(name)) {
         return art::unexpected(Error::Busy);
      }
      if(files_.erase(name) != 0u) {
         return {};
      }
      if(!directories_.contains(name)) {
         return art::unexpected(Error::NoEntry);
      }
      if(hasChildren(name)) {
         return art::unexpected(Error::NotEmpty);
      }
      directories_.erase(name);
      return {};
   }

   art::expected<void, Error> rename(const char *oldPath, const char *newPath) override {
      if(!mounted_) {
         return art::unexpected(Error::NotMounted);
      }
      const std::string from{ oldPath };
      const std::string to{ newPath };
      if(hasTooLongName(to)) {
         return art::unexpected(Error::NameTooLong);
      }
      const auto source = files_.find(from);
      if(source == files_.end()) {
         return art::unexpected(directories_.contains(from) ? Error::Unsupported : Error::NoEntry);
      }
      if(from == to) {
         return {};
      }
      if(isOpenPath(from) || isOpenPath(to)) {
         return art::unexpected(Error::Busy);
      }
      if(directories_.contains(to)) {
         return art::unexpected(Error::IsDirectory);
      }
      if(!directories_.contains(parentOf(to)) && parentOf(to) != "/") {
         return art::unexpected(Error::NoEntry);
      }
      files_[to] = std::move(source->second);
      files_.erase(from);
      return {};
   }

   art::expected<void, Error> mkdir(const char *path) override {
      if(!mounted_) {
         return art::unexpected(Error::NotMounted);
      }
      const std::string name{ path };
      if(hasTooLongName(name)) {
         return art::unexpected(Error::NameTooLong);
      }
      if(files_.contains(name) || directories_.contains(name)) {
         return art::unexpected(Error::Exists);
      }
      if(!directories_.contains(parentOf(name)) && parentOf(name) != "/") {
         return art::unexpected(Error::NoEntry);
      }
      directories_.insert(name);
      return {};
   }

   art::expected<FileInfo, Error> stat(const char *path) override {
      if(!mounted_) {
         return art::unexpected(Error::NotMounted);
      }
      const std::string name{ path };
      if(name == "/" || directories_.contains(name)) {
         return FileInfo{ .type = EntryType::Directory, .size = 0u };
      }
      const auto file = files_.find(name);
      if(file == files_.end()) {
         return art::unexpected(Error::NoEntry);
      }
      return FileInfo{ .type = EntryType::File, .size = file->second.size() };
   }

   art::expected<size_t, Error> visitDirectory(const char *path, DirectoryVisitor visitor) override {
      if(!mounted_) {
         return art::unexpected(Error::NotMounted);
      }
      const std::string directory{ path };
      if(directory != "/" && !directories_.contains(directory)) {
         return art::unexpected(files_.contains(directory) ? Error::NotDirectory : Error::NoEntry);
      }
      size_t visited = 0u;
      for(const auto &name : directories_) {
         if(parentOf(name) == directory) {
            ++visited;
            if(!visitor(baseName(name).c_str(), FileInfo{ .type = EntryType::Directory, .size = 0u })) {
               return visited;
            }
         }
      }
      for(const auto &[name, content] : files_) {
         if(parentOf(name) == directory) {
            ++visited;
            if(!visitor(baseName(name).c_str(), FileInfo{ .type = EntryType::File, .size = content.size() })) {
               return visited;
            }
         }
      }
      return visited;
   }

   art::expected<offset_t, Error> freeSpace() override {
      return mounted_ ? art::expected<offset_t, Error>{ 4096u } : art::unexpected(Error::NotMounted);
   }

   /// Number of files that are currently open.
   [[nodiscard]] size_t openFileCount() const {
      return static_cast<size_t>(std::ranges::count_if(slots_, [](const Slot &slot) { return slot.used; }));
   }

   /// Content of a file for verification; the file must exist.
   [[nodiscard]] const std::vector<uint8_t> &content(const std::string &path) const {
      return files_.at(path);
   }

   void failNextWrite(Error error) {
      nextWriteError_ = error;
   }
   void failNextSync(Error error) {
      nextSyncError_ = error;
   }
   void failNextClose(Error error) {
      nextCloseError_ = error;
   }
   /// Makes File::size() report more than the content holds, so a read hits the end early.
   void inflateReportedSize(offset_t extraBytes) {
      reportedSizeExtra_ = extraBytes;
   }

   [[nodiscard]] size_t openCalls() const {
      return openCalls_;
   }
   [[nodiscard]] size_t closeCalls() const {
      return closeCalls_;
   }

   protected:
   art::expected<size_t, Error> fileRead(FileHandle handle, art::span<uint8_t> buffer) override {
      auto *slot = lookup(handle);
      if(slot == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      if(!canRead(slot->mode)) {
         return art::unexpected(Error::InvalidArgument);
      }
      const auto *file = dataOf(*slot);
      if(file == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      const auto &data = *file;
      if(slot->position >= data.size()) {
         return size_t{ 0u };
      }
      const size_t count = std::min<size_t>(buffer.size(), data.size() - slot->position);
      std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(slot->position), count, buffer.begin());
      slot->position += count;
      return count;
   }

   art::expected<void, Error> fileWrite(FileHandle handle, art::span<const uint8_t> data) override {
      auto *slot = lookup(handle);
      if(slot == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      if(!canWrite(slot->mode)) {
         return art::unexpected(Error::InvalidArgument);
      }
      if(nextWriteError_) {
         return art::unexpected(std::exchange(nextWriteError_, std::nullopt).value());
      }
      auto *file = dataOf(*slot);
      if(file == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      auto &content = *file;
      if(slot->mode == OpenMode::Append) {
         slot->position = content.size();
      }
      if(slot->position + data.size() > content.size()) {
         content.resize(slot->position + data.size(), 0u);
      }
      std::ranges::copy(data, content.begin() + static_cast<std::ptrdiff_t>(slot->position));
      slot->position += data.size();
      return {};
   }

   art::expected<void, Error> fileSeek(FileHandle handle, offset_t position) override {
      auto *slot = lookup(handle);
      if(slot == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      slot->position = static_cast<size_t>(position);
      return {};
   }

   art::expected<offset_t, Error> fileTell(FileHandle handle) override {
      const auto *slot = lookup(handle);
      if(slot == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      return slot->position;
   }

   art::expected<offset_t, Error> fileSize(FileHandle handle) override {
      const auto *slot = lookup(handle);
      if(slot == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      const auto *file = dataOf(*slot);
      if(file == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      return file->size() + reportedSizeExtra_;
   }

   art::expected<void, Error> fileSync(FileHandle handle) override {
      if(lookup(handle) == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      if(nextSyncError_) {
         return art::unexpected(std::exchange(nextSyncError_, std::nullopt).value());
      }
      return {};
   }

   art::expected<void, Error> fileClose(FileHandle handle) override {
      auto *slot = lookup(handle);
      if(slot == nullptr) {
         return art::unexpected(Error::BadHandle);
      }
      ++closeCalls_;
      releaseSlot(*slot);
      if(nextCloseError_) {
         return art::unexpected(std::exchange(nextCloseError_, std::nullopt).value());
      }
      return {};
   }

   private:
   struct Slot {
      bool used{ false };
      uint32_t generation{ 0u };
      std::string path;
      OpenMode mode{ OpenMode::Read };
      size_t position{ 0u };
   };

   // Handle layout: generation in the upper 24 bits, slot index in the lowest 8 bits.
   static FileHandle handleOf(size_t index, const Slot &slot) {
      return (slot.generation << 8u) | static_cast<FileHandle>(index);
   }

   static bool canRead(OpenMode mode) {
      switch(mode) {
      case OpenMode::Read:
      case OpenMode::ReadWrite:
      case OpenMode::ReadWriteTruncate:
      case OpenMode::CreateNew:
         return true;
      case OpenMode::Write:
      case OpenMode::Append:
         return false;
      }
      return false;
   }

   static bool canWrite(OpenMode mode) {
      return mode != OpenMode::Read;
   }

   static bool hasTooLongName(const std::string &path) {
      size_t start = 0u;
      while(start < path.size()) {
         const auto end = std::min(path.find('/', start), path.size());
         if(end - start > kMaxNameLength) {
            return true;
         }
         start = end + 1u;
      }
      return false;
   }

   [[nodiscard]] bool isOpenPath(const std::string &path) const {
      return std::ranges::any_of(slots_, [&](const Slot &slot) { return slot.used && slot.path == path; });
   }

   // Content of the file a slot refers to, or nullptr if the file is gone. Never throws.
   std::vector<uint8_t> *dataOf(const Slot &slot) {
      const auto file = files_.find(slot.path);
      return file == files_.end() ? nullptr : &file->second;
   }

   Slot *lookup(FileHandle handle) {
      const size_t index = handle & 0xFFu;
      if(index >= slots_.size() || !slots_[index].used || slots_[index].generation != (handle >> 8u)) {
         return nullptr;
      }
      return &slots_[index];
   }

   static void releaseSlot(Slot &slot) {
      if(slot.used) {
         slot.used = false;
         ++slot.generation;
      }
   }

   static std::string parentOf(const std::string &path) {
      const auto separator = path.rfind('/');
      return separator == 0u ? "/" : path.substr(0u, separator);
   }

   static std::string baseName(const std::string &path) {
      return path.substr(path.rfind('/') + 1u);
   }

   [[nodiscard]] bool hasChildren(const std::string &directory) const {
      return std::ranges::any_of(files_, [&](const auto &entry) { return parentOf(entry.first) == directory; }) ||
             std::ranges::any_of(directories_, [&](const auto &name) { return parentOf(name) == directory; });
   }

   bool mounted_{ false };
   std::array<Slot, kMaxOpenFiles> slots_{};
   std::map<std::string, std::vector<uint8_t>> files_;
   std::set<std::string> directories_;
   std::optional<Error> nextWriteError_;
   std::optional<Error> nextSyncError_;
   std::optional<Error> nextCloseError_;
   offset_t reportedSizeExtra_{ 0u };
   size_t openCalls_{ 0u };
   size_t closeCalls_{ 0u };
};

} // namespace art::storage::test

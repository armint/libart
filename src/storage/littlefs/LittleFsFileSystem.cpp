#include "storage/littlefs/LittleFsFileSystem.h"
#include "freertos/Mutex.h"
#include "logging/Logger.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace art::storage {

namespace {

static_assert(LFS_NAME_MAX <= kMaxNameLength, "littlefs accepts longer names than the FileSystem interface allows");

logging::Logger &logger() {
   static logging::Logger instance("LittleFs");
   return instance;
}

// Slot index in the lowest 8 bits, generation in the upper 24.
constexpr uint32_t kSlotIndexBits = 8u;
constexpr uint32_t kSlotIndexMask = (1u << kSlotIndexBits) - 1u;
constexpr uint32_t kGenerationMask = 0xFFFFFFu;

// FNV-1a, 32 bit.
constexpr uint32_t kHashBasis = 2166136261u;
constexpr uint32_t kHashPrime = 16777619u;

constexpr uint32_t hashStep(uint32_t hash, char character) {
   return (hash ^ static_cast<uint8_t>(character)) * kHashPrime;
}

/**
 * Validates a path and hashes its canonical form (empty and "." components dropped), which is how the open
 * paths are recognised. Rejects what the FileSystem interface does not define (relative paths, "..") and names
 * that are too long. littlefs would only notice a too long name when it has to create one.
 */
std::expected<uint32_t, Error> hashPath(const char *path) {
   if(path == nullptr || path[0] != '/') {
      return std::unexpected(Error::InvalidArgument);
   }
   uint32_t hash = kHashBasis;
   const char *cursor = path;
   while(*cursor != '\0') {
      while(*cursor == '/') {
         ++cursor;
      }
      const char *const start = cursor;
      while(*cursor != '\0' && *cursor != '/') {
         ++cursor;
      }
      const size_t length = static_cast<size_t>(cursor - start);
      if(length == 0u || (length == 1u && start[0] == '.')) {
         continue;
      }
      if(length == 2u && start[0] == '.' && start[1] == '.') {
         return std::unexpected(Error::InvalidArgument);
      }
      if(length > LFS_NAME_MAX) {
         return std::unexpected(Error::NameTooLong);
      }
      for(const char *character = start; character != cursor; ++character) {
         hash = hashStep(hash, *character);
      }
      hash = hashStep(hash, '/');
   }
   return hash;
}

int toLfsFlags(OpenMode mode) {
   switch(mode) {
   case OpenMode::Read:
      return LFS_O_RDONLY;
   case OpenMode::Write:
      return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
   case OpenMode::Append:
      return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
   case OpenMode::ReadWrite:
      return LFS_O_RDWR | LFS_O_CREAT;
   case OpenMode::ReadWriteTruncate:
      return LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC;
   case OpenMode::CreateNew:
      return LFS_O_RDWR | LFS_O_CREAT | LFS_O_EXCL;
   }
   return LFS_O_RDONLY;
}

bool isReadable(OpenMode mode) {
   return mode != OpenMode::Write && mode != OpenMode::Append;
}

bool isWritable(OpenMode mode) {
   return mode != OpenMode::Read;
}

std::unexpected<Error> failure(Error error) {
   return std::unexpected(error);
}

} // namespace

LittleFsFileSystemBase::LittleFsFileSystemBase(BlockDevice &device, const Buffers &buffers)
   : device_(device)
   , buffers_(buffers) {}

void LittleFsFileSystemBase::shutdown() {
   art::rtos::MutexGuard guard(mutex_);
   static_cast<void>(unmountLocked());
}

// The single place where a littlefs error code becomes an art::storage::Error.
Error LittleFsFileSystemBase::toError(int lfsError) const {
   switch(lfsError) {
   case LFS_ERR_IO:
      return deviceError_.value_or(Error::Io);
   case LFS_ERR_CORRUPT:
      return Error::Corrupt;
   case LFS_ERR_NOENT:
      return Error::NoEntry;
   case LFS_ERR_EXIST:
      return Error::Exists;
   case LFS_ERR_NOTDIR:
      return Error::NotDirectory;
   case LFS_ERR_ISDIR:
      return Error::IsDirectory;
   case LFS_ERR_NOTEMPTY:
      return Error::NotEmpty;
   case LFS_ERR_BADF:
      return Error::BadHandle;
   case LFS_ERR_FBIG:
      return Error::TooBig;
   case LFS_ERR_INVAL:
      return Error::InvalidArgument;
   case LFS_ERR_NOSPC:
      return Error::NoSpace;
   case LFS_ERR_NOMEM:
      return Error::NoMemory;
   case LFS_ERR_NAMETOOLONG:
      return Error::NameTooLong;
   default:
      // LFS_ERR_NOATTR (attributes are not used) and anything a future littlefs adds.
      return Error::Io;
   }
}

// ---- block device callbacks ----

// clang-format off
int LittleFsFileSystemBase::readBlock(const lfs_config *cfg, lfs_block_t block, lfs_off_t off, void *buf,
                                      lfs_size_t size) {
// clang-format on
   auto &self = *static_cast<LittleFsFileSystemBase *>(cfg->context);
   const offset_t position = static_cast<offset_t>(block) * cfg->block_size + off;
   const auto result = self.device_.read(position, { static_cast<uint8_t *>(buf), size });
   if(!result) {
      self.deviceError_ = result.error();
      return LFS_ERR_IO;
   }
   return LFS_ERR_OK;
}

// clang-format off
int LittleFsFileSystemBase::programBlock(const lfs_config *cfg, lfs_block_t block, lfs_off_t off, const void *buf,
                                         lfs_size_t size) {
// clang-format on
   auto &self = *static_cast<LittleFsFileSystemBase *>(cfg->context);
   const offset_t position = static_cast<offset_t>(block) * cfg->block_size + off;
   const auto result = self.device_.program(position, { static_cast<const uint8_t *>(buf), size });
   if(!result) {
      self.deviceError_ = result.error();
      return LFS_ERR_IO;
   }
   return LFS_ERR_OK;
}

int LittleFsFileSystemBase::eraseBlock(const lfs_config *cfg, lfs_block_t block) {
   auto &self = *static_cast<LittleFsFileSystemBase *>(cfg->context);
   const auto result = self.device_.erase(static_cast<offset_t>(block) * cfg->block_size, cfg->block_size);
   if(!result) {
      self.deviceError_ = result.error();
      return LFS_ERR_IO;
   }
   return LFS_ERR_OK;
}

int LittleFsFileSystemBase::syncDevice(const lfs_config *cfg) {
   auto &self = *static_cast<LittleFsFileSystemBase *>(cfg->context);
   const auto result = self.device_.sync();
   if(!result) {
      self.deviceError_ = result.error();
      return LFS_ERR_IO;
   }
   return LFS_ERR_OK;
}

// ---- mount, unmount, format ----

std::expected<void, Error> LittleFsFileSystemBase::configure() {
   if(const auto initialised = device_.init(); !initialised) {
      LOG_ERROR(logger(), "device init failed: %s", toString(initialised.error()));
      return failure(initialised.error());
   }
   const Geometry geometry = device_.geometry();
   const uint32_t cacheSize = static_cast<uint32_t>(buffers_.readCache.size());
   const uint32_t lookaheadSize = static_cast<uint32_t>(buffers_.lookahead.size());

   const char *problem = nullptr;
   if(!geometry.isValid()) {
      problem = "device geometry is not self-consistent";
   } else if(geometry.eraseSize == 0u) {
      problem = "device has no erase unit, which littlefs needs as block size";
   } else if(geometry.eraseSize < 128u) {
      problem = "erase unit is smaller than the 128 bytes littlefs needs";
   } else if(geometry.size / geometry.eraseSize < 2u ||
             geometry.size / geometry.eraseSize > std::numeric_limits<lfs_size_t>::max()) {
      problem = "block count is outside 2..2^32-1";
   } else if(cacheSize % geometry.readSize != 0u || cacheSize % geometry.programSize != 0u) {
      problem = "cache size is not a multiple of the read and program size";
   } else if(geometry.eraseSize % cacheSize != 0u) {
      problem = "cache size does not divide the erase unit";
   } else if(lookaheadSize == 0u || lookaheadSize % 8u != 0u) {
      problem = "lookahead size is not a positive multiple of 8";
   }
   if(problem != nullptr) {
      LOG_ERROR(logger(), "cannot mount: %s (read %u, program %u, erase %u, cache %u)", problem, geometry.readSize,
                geometry.programSize, geometry.eraseSize, cacheSize);
      return failure(Error::InvalidArgument);
   }

   geometry_ = geometry;
   config_ = {};
   config_.context = this;
   config_.read = &readBlock;
   config_.prog = &programBlock;
   config_.erase = &eraseBlock;
   config_.sync = &syncDevice;
   config_.read_size = geometry.readSize;
   config_.prog_size = geometry.programSize;
   config_.block_size = geometry.eraseSize;
   config_.block_count = static_cast<lfs_size_t>(geometry.size / geometry.eraseSize);
   config_.block_cycles = kBlockCycles;
   config_.cache_size = cacheSize;
   config_.lookahead_size = lookaheadSize;
   config_.read_buffer = buffers_.readCache.data();
   config_.prog_buffer = buffers_.programCache.data();
   config_.lookahead_buffer = buffers_.lookahead.data();
   return {};
}

std::expected<void, Error> LittleFsFileSystemBase::mountLocked() {
   if(mounted_) {
      return {};
   }
   if(const auto configured = configure(); !configured) {
      return configured;
   }
   deviceError_.reset();
   int result = lfs_mount(&lfs_, &config_);
   if(result == LFS_ERR_CORRUPT) {
      LOG_WARN(logger(), "no valid volume found, formatting");
      result = lfs_format(&lfs_, &config_);
      if(result == LFS_ERR_OK) {
         result = lfs_mount(&lfs_, &config_);
      }
   }
   if(result != LFS_ERR_OK) {
      LOG_ERROR(logger(), "mount failed: %d", result);
      return failure(toError(result));
   }
   mounted_ = true;
   return {};
}

std::expected<void, Error> LittleFsFileSystemBase::mount() {
   art::rtos::MutexGuard guard(mutex_);
   return mountLocked();
}

std::expected<void, Error> LittleFsFileSystemBase::unmountLocked() {
   if(!mounted_) {
      return {};
   }
   deviceError_.reset();
   std::expected<void, Error> outcome;
   for(auto &slot : buffers_.slots) {
      if(slot.used) {
         if(const auto closed = closeSlot(slot); !closed && outcome) {
            outcome = closed;
         }
      }
   }
   const int result = lfs_unmount(&lfs_);
   mounted_ = false;
   if(result != LFS_ERR_OK && outcome) {
      outcome = failure(toError(result));
   }
   return outcome;
}

std::expected<void, Error> LittleFsFileSystemBase::unmount() {
   art::rtos::MutexGuard guard(mutex_);
   return unmountLocked();
}

std::expected<void, Error> LittleFsFileSystemBase::format() {
   art::rtos::MutexGuard guard(mutex_);
   if(mounted_) {
      return failure(Error::Busy);
   }
   if(const auto configured = configure(); !configured) {
      return configured;
   }
   deviceError_.reset();
   const int result = lfs_format(&lfs_, &config_);
   if(result != LFS_ERR_OK) {
      return failure(toError(result));
   }
   return {};
}

bool LittleFsFileSystemBase::isMounted() const {
   art::rtos::MutexGuard guard(mutex_);
   return mounted_;
}

// ---- handle pool ----

FileHandle LittleFsFileSystemBase::handleOf(size_t index) const {
   return ((buffers_.slots[index].generation & kGenerationMask) << kSlotIndexBits) | static_cast<FileHandle>(index);
}

LittleFsFileSystemBase::FileSlot *LittleFsFileSystemBase::lookup(FileHandle handle) {
   const size_t index = handle & kSlotIndexMask;
   if(!mounted_ || index >= buffers_.slots.size()) {
      return nullptr;
   }
   FileSlot &slot = buffers_.slots[index];
   return slot.used && (slot.generation & kGenerationMask) == (handle >> kSlotIndexBits) ? &slot : nullptr;
}

uint8_t *LittleFsFileSystemBase::cacheOf(size_t index) const {
   return buffers_.fileCaches.data() + index * buffers_.readCache.size();
}

// Closes the littlefs file and releases the slot, whatever the outcome of the final flush.
std::expected<void, Error> LittleFsFileSystemBase::closeSlot(FileSlot &slot) {
   const int result = lfs_file_close(&lfs_, &slot.file);
   slot.used = false;
   slot.generation = (slot.generation + 1u) & kGenerationMask;
   if(result != LFS_ERR_OK) {
      return failure(toError(result));
   }
   return {};
}

bool LittleFsFileSystemBase::isPathOpen(uint32_t pathHash) const {
   return std::ranges::any_of(buffers_.slots,
                              [pathHash](const FileSlot &slot) { return slot.used && slot.pathHash == pathHash; });
}

// ---- namespace operations ----

std::expected<File, Error> LittleFsFileSystemBase::open(const char *path, OpenMode mode) {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   const auto pathHash = hashPath(path);
   if(!pathHash) {
      return failure(pathHash.error());
   }
   const bool writable = isWritable(mode);
   for(const auto &slot : buffers_.slots) {
      if(slot.used && slot.pathHash == *pathHash && (writable || slot.writable)) {
         return failure(mode == OpenMode::CreateNew ? Error::Exists : Error::Busy);
      }
   }
   const auto freeSlot = std::ranges::find_if(buffers_.slots, [](const FileSlot &slot) { return !slot.used; });
   if(freeSlot == buffers_.slots.end()) {
      return failure(Error::NoMemory);
   }
   const size_t index = static_cast<size_t>(freeSlot - buffers_.slots.begin());

   deviceError_.reset();
   freeSlot->config = {};
   freeSlot->config.buffer = cacheOf(index);
   int result = lfs_file_opencfg(&lfs_, &freeSlot->file, path, toLfsFlags(mode), &freeSlot->config);
   if(result < 0) {
      return failure(toError(result));
   }
   if(mode == OpenMode::Append) {
      result = lfs_file_seek(&lfs_, &freeSlot->file, 0, LFS_SEEK_END);
      if(result < 0) {
         const Error error = toError(result);
         static_cast<void>(lfs_file_close(&lfs_, &freeSlot->file));
         return failure(error);
      }
   }
   freeSlot->used = true;
   freeSlot->pathHash = *pathHash;
   freeSlot->readable = isReadable(mode);
   freeSlot->writable = writable;
   return makeFile(handleOf(index));
}

std::expected<void, Error> LittleFsFileSystemBase::remove(const char *path) {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   const auto pathHash = hashPath(path);
   if(!pathHash) {
      return failure(pathHash.error());
   }
   if(isPathOpen(*pathHash)) {
      return failure(Error::Busy);
   }
   deviceError_.reset();
   const int result = lfs_remove(&lfs_, path);
   return result < 0 ? std::expected<void, Error>{ failure(toError(result)) } : std::expected<void, Error>{};
}

std::expected<void, Error> LittleFsFileSystemBase::rename(const char *oldPath, const char *newPath) {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   const auto oldHash = hashPath(oldPath);
   if(!oldHash) {
      return failure(oldHash.error());
   }
   const auto newHash = hashPath(newPath);
   if(!newHash) {
      return failure(newHash.error());
   }
   // Renaming an entry onto itself is a no-op in littlefs and safe for an open file.
   if(*oldHash != *newHash && (isPathOpen(*oldHash) || isPathOpen(*newHash))) {
      return failure(Error::Busy);
   }
   deviceError_.reset();
   const int result = lfs_rename(&lfs_, oldPath, newPath);
   return result < 0 ? std::expected<void, Error>{ failure(toError(result)) } : std::expected<void, Error>{};
}

std::expected<void, Error> LittleFsFileSystemBase::mkdir(const char *path) {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   if(const auto pathHash = hashPath(path); !pathHash) {
      return failure(pathHash.error());
   }
   deviceError_.reset();
   const int result = lfs_mkdir(&lfs_, path);
   return result < 0 ? std::expected<void, Error>{ failure(toError(result)) } : std::expected<void, Error>{};
}

std::expected<FileInfo, Error> LittleFsFileSystemBase::stat(const char *path) {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   if(const auto pathHash = hashPath(path); !pathHash) {
      return failure(pathHash.error());
   }
   deviceError_.reset();
   lfs_info info;
   const int result = lfs_stat(&lfs_, path, &info);
   if(result < 0) {
      return failure(toError(result));
   }
   return FileInfo{ .type = info.type == LFS_TYPE_DIR ? EntryType::Directory : EntryType::File,
                    .size = info.type == LFS_TYPE_DIR ? 0u : info.size };
}

std::expected<size_t, Error> LittleFsFileSystemBase::visitDirectory(const char *path, DirectoryVisitor visitor) {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   if(const auto pathHash = hashPath(path); !pathHash) {
      return failure(pathHash.error());
   }
   if(!visitor.is_valid()) {
      return failure(Error::InvalidArgument);
   }
   deviceError_.reset();
   lfs_dir_t directory;
   int result = lfs_dir_open(&lfs_, &directory, path);
   if(result < 0) {
      return failure(toError(result));
   }
   size_t visited = 0u;
   lfs_info info;
   while((result = lfs_dir_read(&lfs_, &directory, &info)) > 0) {
      if(std::strcmp(info.name, ".") == 0 || std::strcmp(info.name, "..") == 0) {
         continue;
      }
      ++visited;
      const bool isDirectory = info.type == LFS_TYPE_DIR;
      if(!visitor(info.name, FileInfo{ .type = isDirectory ? EntryType::Directory : EntryType::File,
                                       .size = isDirectory ? 0u : info.size })) {
         break;
      }
   }
   const Error readError = result < 0 ? toError(result) : Error::Io;
   static_cast<void>(lfs_dir_close(&lfs_, &directory));
   if(result < 0) {
      return failure(readError);
   }
   return visited;
}

std::expected<offset_t, Error> LittleFsFileSystemBase::freeSpace() {
   art::rtos::MutexGuard guard(mutex_);
   if(!mounted_) {
      return failure(Error::NotMounted);
   }
   deviceError_.reset();
   const lfs_ssize_t usedBlocks = lfs_fs_size(&lfs_);
   if(usedBlocks < 0) {
      return failure(toError(usedBlocks));
   }
   const offset_t totalBlocks = config_.block_count;
   const offset_t used = std::min<offset_t>(static_cast<offset_t>(usedBlocks), totalBlocks);
   return (totalBlocks - used) * config_.block_size;
}

// ---- file hooks ----

std::expected<size_t, Error> LittleFsFileSystemBase::fileRead(FileHandle handle, std::span<uint8_t> buffer) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   if(!slot->readable) {
      return failure(Error::InvalidArgument);
   }
   deviceError_.reset();
   const auto count = static_cast<lfs_size_t>(std::min<size_t>(buffer.size(), std::numeric_limits<int32_t>::max()));
   const lfs_ssize_t result = lfs_file_read(&lfs_, &slot->file, buffer.data(), count);
   if(result < 0) {
      return failure(toError(result));
   }
   return static_cast<size_t>(result);
}

std::expected<void, Error> LittleFsFileSystemBase::fileWrite(FileHandle handle, std::span<const uint8_t> data) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   if(!slot->writable) {
      return failure(Error::InvalidArgument);
   }
   if(data.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
      return failure(Error::TooBig);
   }
   deviceError_.reset();
   const lfs_ssize_t result = lfs_file_write(&lfs_, &slot->file, data.data(), static_cast<lfs_size_t>(data.size()));
   if(result < 0) {
      return failure(toError(result));
   }
   // littlefs reports NOSPC as an error; a short count is treated the same way, never as success.
   return static_cast<size_t>(result) == data.size() ? std::expected<void, Error>{} : failure(Error::NoSpace);
}

std::expected<void, Error> LittleFsFileSystemBase::fileSeek(FileHandle handle, offset_t position) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   if(position > lfs_.file_max) {
      return failure(Error::TooBig);
   }
   deviceError_.reset();
   const lfs_soff_t result = lfs_file_seek(&lfs_, &slot->file, static_cast<lfs_soff_t>(position), LFS_SEEK_SET);
   return result < 0 ? std::expected<void, Error>{ failure(toError(result)) } : std::expected<void, Error>{};
}

std::expected<offset_t, Error> LittleFsFileSystemBase::fileTell(FileHandle handle) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   const lfs_soff_t position = lfs_file_tell(&lfs_, &slot->file);
   if(position < 0) {
      return failure(toError(position));
   }
   return static_cast<offset_t>(position);
}

std::expected<offset_t, Error> LittleFsFileSystemBase::fileSize(FileHandle handle) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   const lfs_soff_t size = lfs_file_size(&lfs_, &slot->file);
   if(size < 0) {
      return failure(toError(size));
   }
   return static_cast<offset_t>(size);
}

std::expected<void, Error> LittleFsFileSystemBase::fileSync(FileHandle handle) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   deviceError_.reset();
   const int result = lfs_file_sync(&lfs_, &slot->file);
   return result < 0 ? std::expected<void, Error>{ failure(toError(result)) } : std::expected<void, Error>{};
}

std::expected<void, Error> LittleFsFileSystemBase::fileClose(FileHandle handle) {
   art::rtos::MutexGuard guard(mutex_);
   FileSlot *slot = lookup(handle);
   if(slot == nullptr) {
      return failure(Error::BadHandle);
   }
   deviceError_.reset();
   return closeSlot(*slot);
}

} // namespace art::storage

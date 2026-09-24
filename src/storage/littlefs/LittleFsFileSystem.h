#pragma once

#include "freertos/Mutex.h"
#include "storage/BlockDevice.h"
#include "storage/FileSystem.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <lfs.h>
#include <optional>
#include <span>

namespace art::storage {

/**
 * @brief The non-template part of LittleFsFileSystem. Use the template; this class only keeps the code
 * out of every instantiation.
 *
 * All static buffers are owned by the derived template and handed in as spans, so this class allocates
 * nothing (littlefs is built with LFS_NO_MALLOC).
 */
class LittleFsFileSystemBase : public FileSystem {
   public:
   /// Block cycles before littlefs evicts a metadata log as part of wear levelling (its suggested range
   /// is 100 to 1000).
   static constexpr int32_t kBlockCycles = 500;

   std::expected<void, Error> mount() override;
   std::expected<void, Error> unmount() override;
   std::expected<void, Error> format() override;
   [[nodiscard]] bool isMounted() const override;
   std::expected<File, Error> open(const char *path, OpenMode mode) override;
   std::expected<void, Error> remove(const char *path) override;
   std::expected<void, Error> rename(const char *oldPath, const char *newPath) override;
   std::expected<void, Error> mkdir(const char *path) override;
   std::expected<FileInfo, Error> stat(const char *path) override;
   std::expected<size_t, Error> visitDirectory(const char *path, DirectoryVisitor visitor) override;
   std::expected<offset_t, Error> freeSpace() override;

   protected:
   /// State of one entry of the static open-file pool.
   struct FileSlot {
      lfs_file_t file{};
      lfs_file_config config{};
      uint32_t pathHash{ 0u };
      uint32_t generation{ 0u };
      /// The error of the last failed call on this file. lfs_file_sync() and lfs_file_close() return success for a
      /// file that littlefs marked as errored (LFS_F_ERRED), so the binding reports this error in their place.
      Error lastError{ Error::Io };
      bool used{ false };
      bool readable{ false };
      bool writable{ false };
   };

   /// The static memory the derived template provides. Only its addresses are used during construction.
   struct Buffers {
      std::span<uint8_t> readCache;
      std::span<uint8_t> programCache;
      std::span<uint8_t> lookahead;
      std::span<FileSlot> slots;
      /// slots.size() consecutive caches of readCache.size() bytes each.
      std::span<uint8_t> fileCaches;
   };

   LittleFsFileSystemBase(BlockDevice &device, const Buffers &buffers);

   /// Closes all files and unmounts. The derived destructor calls it while its buffers are still alive.
   void shutdown();

   std::expected<size_t, Error> fileRead(FileHandle handle, std::span<uint8_t> buffer) override;
   std::expected<void, Error> fileWrite(FileHandle handle, std::span<const uint8_t> data) override;
   std::expected<void, Error> fileSeek(FileHandle handle, offset_t position) override;
   std::expected<offset_t, Error> fileTell(FileHandle handle) override;
   std::expected<offset_t, Error> fileSize(FileHandle handle) override;
   std::expected<void, Error> fileSync(FileHandle handle) override;
   std::expected<void, Error> fileClose(FileHandle handle) override;

   private:
   std::expected<void, Error> mountLocked();
   std::expected<void, Error> unmountLocked();
   std::expected<void, Error> configure();
   FileSlot *lookup(FileHandle handle);
   FileHandle handleOf(size_t index) const;
   uint8_t *cacheOf(size_t index) const;
   std::expected<void, Error> closeSlot(FileSlot &slot);
   Error noteFailure(FileSlot &slot, int lfsError);
   static bool isErrored(const FileSlot &slot);
   bool isPathOpen(uint32_t pathHash) const;
   void dropReadCaches();
   void resetProgramCache();
   Error failed(int lfsError);
   Error toError(int lfsError) const;

   static int lfsGet(const lfs_config *c, lfs_block_t blk, lfs_off_t at, void *buf, lfs_size_t n);
   static int lfsPut(const lfs_config *c, lfs_block_t blk, lfs_off_t at, const void *buf, lfs_size_t n);
   static int lfsErase(const lfs_config *c, lfs_block_t blk);
   static int lfsSync(const lfs_config *c);

   BlockDevice &device_;
   Buffers buffers_;
   mutable art::rtos::Mutex mutex_;
   lfs_t lfs_{};
   lfs_config config_{};
   Geometry geometry_{};
   bool mounted_{ false };
   /// The last error a device call returned, so that a transient failure (Timeout, Busy, ...) is not
   /// reported as a plain Io. Reset at the start of every operation.
   std::optional<Error> deviceError_;
};

/**
 * @brief FileSystem implementation on top of littlefs and a BlockDevice.
 *
 * @tparam MaxOpenFiles Size of the static open-file pool. open() fails with Error::NoMemory when it is
 * exhausted. At most 255.
 * @tparam CacheSize Bytes of the read cache, the program cache and of each open file's cache. It must be a
 * multiple of the device's readSize and programSize and divide its eraseSize; mount() checks this.
 * @tparam LookaheadSize Bytes of the block allocator's lookahead bitmap, a multiple of 8. Each byte covers
 * 8 blocks per allocator scan; more is faster on large volumes and costs RAM.
 *
 * RAM use is 2 * CacheSize + LookaheadSize + MaxOpenFiles * (CacheSize + sizeof(FileSlot)) plus the lfs_t.
 *
 * @par Geometry
 * The layout comes from BlockDevice::geometry() at mount time; nothing is hardcoded. littlefs' read size,
 * program size and block size are the device's readSize, programSize and eraseSize, and its block count is
 * size / eraseSize. mount() and format() reject a device without erase unit, or a geometry that violates the
 * cache constraints above, with Error::InvalidArgument and log the reason, instead of handing littlefs values
 * it only asserts on. A device that reports a coarser readSize (a transform device) works unchanged.
 *
 * @par Mount and format
 * mount() calls BlockDevice::init() (which is idempotent), then mounts. If littlefs reports the volume as
 * corrupt, which is what a blank chip (all 0xFF) or a chip with another file system looks like, mount()
 * formats the device once and mounts again, so a boot never fails on an unformatted flash. Other errors,
 * such as an Io error while reading, do not trigger a format. Note that this destroys whatever a corrupt but
 * recoverable volume held.
 *
 * @par Serialization
 * One art::rtos::Mutex, taken by every public method and every file hook, serializes all use, because
 * littlefs is not re-entrant. It is not recursive: a DirectoryVisitor that calls back into the file system
 * deadlocks.
 *
 * @par Open files
 * - Handles are pool indices with a generation counter; a closed, unmounted or reused handle yields
 *   Error::BadHandle.
 * - littlefs itself cannot refuse to remove or rename an open file: its open handle then silently loses
 *   later writes (the entry it belongs to is gone) and may read blocks that were reused. To stay inside the
 *   "never undefined" bound of FileSystem::remove(), this class refuses instead: remove() and rename() of a
 *   path that is open, and rename() onto an open path, fail with Error::Busy.
 * - The same protection applies to two handles on one file, because they would not see each other's data:
 *   open() fails with Error::Busy if the path is open and either handle writes (CreateNew reports
 *   Error::Exists). Any number of read-only handles is fine.
 * - Open paths are recognised by a 32 bit hash of the path with empty and "." components removed, so
 *   different spellings of one path match. A hash collision can make an unrelated path report Busy. Paths
 *   with ".." components are rejected with Error::InvalidArgument.
 *
 * @par Failed device reads
 * A read that fails on the device is reported as its own error (Timeout, Io, ...). A read operation (File::read(),
 * stat(), visitDirectory(), freeSpace()) can be repeated once the device works again. littlefs alone does not
 * allow that: it keeps a range marked as cached after the read that should have filled it failed, so the repeat
 * could return stale bytes. The binding drops the read caches whenever the device fails a read; see
 * dropReadCaches() in LittleFsFileSystem.cpp. This does not extend to a write that failed on an internal read:
 * littlefs also leaves its program cache dirty then, and a repeat can trip one of its own assertions unless
 * they are compiled out (ART_LITTLEFS_NO_ASSERT).
 *
 * @par Stack use
 * visitDirectory() and stat() keep an lfs_info (about 260 bytes) and an lfs_dir_t on the stack.
 */
template <size_t MaxOpenFiles = 2u, size_t CacheSize = 256u, size_t LookaheadSize = 128u>
class LittleFsFileSystem final : public LittleFsFileSystemBase {
   static_assert(MaxOpenFiles >= 1u && MaxOpenFiles <= 255u, "the handle encodes the slot index in 8 bits");
   static_assert(CacheSize > 0u, "CacheSize must not be 0");
   static_assert(LookaheadSize > 0u && LookaheadSize % 8u == 0u, "LookaheadSize must be a multiple of 8");

   public:
   /// @param device The device to keep the volume on. It must outlive this object. It does not have to be
   /// initialised: mount() and format() call init().
   explicit LittleFsFileSystem(BlockDevice &device)
      : LittleFsFileSystemBase(device,
                               Buffers{ .readCache = readCache_,
                                        .programCache = programCache_,
                                        .lookahead = lookahead_,
                                        .slots = slots_,
                                        .fileCaches = fileCaches_ }) {}

   ~LittleFsFileSystem() override {
      shutdown();
   }

   private:
   std::array<uint8_t, CacheSize> readCache_{};
   std::array<uint8_t, CacheSize> programCache_{};
   std::array<uint8_t, LookaheadSize> lookahead_{};
   std::array<FileSlot, MaxOpenFiles> slots_{};
   std::array<uint8_t, MaxOpenFiles * CacheSize> fileCaches_{};
};

} // namespace art::storage

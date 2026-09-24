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
   void releaseSlot(FileSlot &slot);
   Error noteFailure(FileSlot &slot, int lfsError, bool mutating);
   static bool isErrored(const FileSlot &slot);
   bool isPathOpen(uint32_t pathHash) const;
   void dropReadCaches();
   void resetProgramCache();
   Error failed(int lfsError);
   Error failedMutation(int lfsError, const FileSlot *self, bool leavesPendingMove = false);
   bool hasOtherWriter(const FileSlot *self) const;
   std::optional<Error> refusal() const;
   Error toError(int lfsError) const;

   static int lfsGet(const lfs_config *c, lfs_block_t blk, lfs_off_t at, void *buf, lfs_size_t n);
   static int lfsPut(const lfs_config *c, lfs_block_t blk, lfs_off_t at, const void *buf, lfs_size_t n);
   static int lfsErase(const lfs_config *c, lfs_block_t blk);
   static int lfsSync(const lfs_config *c);

   /// Lets a test look at the state of littlefs behind the binding (the program cache after a failed call).
   friend struct LittleFsFileSystemTestAccess;

   BlockDevice &device_;
   Buffers buffers_;
   mutable art::rtos::Mutex mutex_;
   lfs_t lfs_{};
   lfs_config config_{};
   Geometry geometry_{};
   bool mounted_{ false };
   /// Set when littlefs may hold state that no longer matches the medium (see "Poisoned mount" in
   /// LittleFsFileSystem). Every call but unmount() is then refused with Error::NeedsRemount. Only true while
   /// mounted_ is.
   bool poisoned_{ false };
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
 * - Other path rules are littlefs': a slash after the last name of a directory is ignored, after a file it is
 *   Error::NotDirectory (and creates nothing), so "/dir/" and "/dir" are the same directory and "/file/" is not
 *   the file. littlefs before v2.10.0 took a slash after a file and stored it in a new name.
 *
 * @par Supported littlefs versions
 * v2.11.3 (the pin of libArT) and v2.9.3, which a superproject may still pin. The binding depends on littlefs
 * internals and on its path parser, and only these two versions were reviewed; any other minor version is a compile
 * error (static_assert in LittleFsFileSystem.cpp). With v2.9.x the trailing-slash rule above does not hold.
 *
 * @par Failed device operations
 * A call that fails on the device is reported as its own error (Timeout, Io, ...), and the mount stays usable: once
 * the device works again, the next call runs and the failed call can be repeated. That covers the read operations
 * (File::read(), readFile(), open(), stat(), visitDirectory(), freeSpace()) and the write operations (writeFile(),
 * File::write(), File::sync(), File::close(), rename(), remove(), mkdir()). mount() and format() always start
 * with empty caches. littlefs alone does not allow this, for two reasons that the binding handles:
 * - After a failed read it keeps a range marked as cached that the read never filled, so a repeat could return
 *   stale bytes. The binding drops the read caches whenever the device fails a read (dropReadCaches() in
 *   LittleFsFileSystem.cpp).
 * - A call that fails part way through a write leaves the program cache filled. The next write then trips an
 *   assertion of littlefs, or, with the assertions compiled out (ART_LITTLEFS_NO_ASSERT), can program stale data.
 *   The binding clears the program cache at the end of every failed call that can write (resetProgramCache()). The
 *   cache of an open file is not touched, because it holds that file's unwritten data.
 *
 * A failed write call may have taken effect nevertheless, because littlefs commits first and can still fail on a
 * read that follows. Repeating it then reports Error::NoEntry (rename(), remove()) or Error::Exists (mkdir())
 * instead of success. A File whose write failed keeps reporting the error from sync() and close() until a later
 * write on it succeeds.
 *
 * This holds unless the mount is poisoned, which happens in two situations (next paragraph). The tests behind it
 * (TestLittleFsReadCache.cpp, TestLittleFsProgramCache.cpp) fail a device read at every position of one call at a
 * time, with no other file open for writing, and rename() only within one directory. The two situations outside of
 * them are exactly the two of the poisoned mount.
 *
 * @par Poisoned mount
 * Two situations leave littlefs with state in memory that no longer matches the medium, and the binding cannot
 * inspect or repair it. Both behave the same with littlefs v2.11.3 as with v2.9.3, so the pin does not change this:
 * - A failed rename() into another directory. It commits twice, and a read that fails in between leaves littlefs with
 *   a pending move. A repeat of the rename can then report success and later commits lose entries at the next mount,
 *   or trip an assertion of littlefs (`dir->count > 0` in lfs_dir_relocatingcommit()), which continues with a wrong
 *   count when assertions are compiled out.
 * - A failed change (open for writing, File::write(), File::sync(), File::close(), rename(), remove(), mkdir())
 *   while another file is open for writing. One read position was observed to lose the data of that file at its
 *   close(), although the close reports success (issue #15).
 *
 * The binding then poisons the mount: every call is refused with Error::NeedsRemount until unmount() and mount().
 * - Refused are all methods and all File operations, including the retry of the failed call, open() and stat()
 *   (which read only), and the operations on the handles that were open. mount() is refused as well, because the
 *   file system is mounted; it does not clear the state. format() fails with Error::Busy like on any mounted file
 *   system. isMounted() stays true. File::close() reports Error::NeedsRemount, and its handle is invalid afterwards
 *   like after any close(), but the file slot is only released by unmount().
 * - unmount() is safe and returns success. It does not commit anything, in particular it does not close the writers
 *   through littlefs: it releases the file slots and unmounts. Every File that was open becomes stale
 *   (Error::BadHandle), and what a writer has not synced is lost. The destructor does the same.
 * - The state is per mount. unmount() ends it, and the next mount() starts from the medium like after a power cut.
 * - A failure that littlefs reports before it changed anything does not poison the mount: NoEntry, Exists,
 *   NotDirectory, IsDirectory and NotEmpty (and the errors that the binding reports itself, such as Busy or
 *   InvalidArgument). Every other failure of the calls above poisons the mount in the two situations, including
 *   NoSpace and Corrupt. Anywhere else it does not: not with no other file open for writing (a rename into another
 *   directory excepted), and not for a failed read (File::read(), File::seek(), stat(), visitDirectory(),
 *   freeSpace(), open() for reading) with a writer open, which cost the writer no data in the tests.
 *
 * What a remount finds (TestLittleFsPoison.cpp fails a read, or a read, program or erase, at every position of the
 * call, and remounts at once):
 * - The volume is consistent and takes writes. A call that was interrupted has taken effect completely or not at all,
 *   and no other file changed. That was checked at 3516 failing positions of eleven calls with a writer open (a new
 *   file, and an existing one that was truncated or appended to, each with and without a sync in between) and at
 *   1380 failing positions of five renames into another directory.
 * - A writer keeps what it had synced, and nothing else: a new file exists and is empty, or holds the part written up
 *   to its last sync(); an existing file that was truncated by open() or appended to holds the content it had before
 *   it was opened (plus the synced part for Append). Everything written since the last sync is lost, exactly like
 *   after a power cut.
 * - The first change after such a mount completes an interrupted rename() into another directory (littlefs applies
 *   a pending move lazily). Until then a read can miss the entry that follows the moved one in its directory, at 27
 *   of 179 read positions of rename("/old", "/dir/moved") in the test volume. After that change the moved entry is
 *   in exactly one of the two places. After such a failure, make a change before reading anything that matters.
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

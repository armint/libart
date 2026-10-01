#pragma once

#include "freertos/Mutex.h"
#include "storage/BlockDevice.h"
#include "storage/FileSystem.h"
#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <lfs.h>
#include <optional>

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

   art::expected<void, Error> mount() override;
   art::expected<void, Error> unmount() override;
   art::expected<void, Error> format() override;
   [[nodiscard]] bool isMounted() const override;
   art::expected<File, Error> open(const char *path, OpenMode mode) override;
   art::expected<void, Error> remove(const char *path) override;
   art::expected<void, Error> rename(const char *oldPath, const char *newPath) override;
   art::expected<void, Error> mkdir(const char *path) override;
   art::expected<FileInfo, Error> stat(const char *path) override;
   art::expected<size_t, Error> visitDirectory(const char *path, DirectoryVisitor visitor) override;
   art::expected<offset_t, Error> freeSpace() override;

   /**
    * @brief Completes the changes littlefs left pending on the medium, so that reads see every entry.
    *
    * See "Consistency after a remount" in LittleFsFileSystem. Not part of FileSystem: no other file system needs it.
    * Writes only if something is pending; on a consistent volume it reads and changes nothing.
    *
    * @return Error::NotMounted if the file system is not mounted and Error::NeedsRemount if the mount is poisoned
    * (nothing is attempted then). A failure of the repair itself (Io, Timeout, NoSpace, Corrupt, ...) is reported like
    * any other and poisons the mount: unmount() and mount(), and go on without the repair (the volume is readable) or
    * try it once more later. Do not retry in a loop, see "Boot sequence" in "Consistency after a remount".
    */
   art::expected<void, Error> makeConsistent();

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
      art::span<uint8_t> readCache;
      art::span<uint8_t> programCache;
      art::span<uint8_t> lookahead;
      art::span<FileSlot> slots;
      /// slots.size() consecutive caches of readCache.size() bytes each.
      art::span<uint8_t> fileCaches;
   };

   LittleFsFileSystemBase(BlockDevice &device, const Buffers &buffers);

   /// Closes all files and unmounts. The derived destructor calls it while its buffers are still alive.
   void shutdown();

   art::expected<size_t, Error> fileRead(FileHandle handle, art::span<uint8_t> buffer) override;
   art::expected<void, Error> fileWrite(FileHandle handle, art::span<const uint8_t> data) override;
   art::expected<void, Error> fileSeek(FileHandle handle, offset_t position) override;
   art::expected<offset_t, Error> fileTell(FileHandle handle) override;
   art::expected<offset_t, Error> fileSize(FileHandle handle) override;
   art::expected<void, Error> fileSync(FileHandle handle) override;
   art::expected<void, Error> fileClose(FileHandle handle) override;

   private:
   art::expected<void, Error> mountLocked();
   art::expected<void, Error> unmountLocked();
   art::expected<void, Error> configure();
   FileSlot *lookup(FileHandle handle);
   FileHandle handleOf(size_t index) const;
   uint8_t *cacheOf(size_t index) const;
   art::expected<void, Error> closeSlot(FileSlot &slot);
   void releaseSlot(FileSlot &slot);
   Error noteFailure(FileSlot &slot, int lfsError, bool mutating);
   static bool isErrored(const FileSlot &slot);
   bool isPathOpen(uint32_t pathHash) const;
   void dropReadCaches();
   void resetProgramCache();
   Error failed(int lfsError);
   Error failedMutation(int lfsError, const FileSlot *self, const char *multiCommitCall = nullptr);
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
 * File::write(), File::sync(), File::close(), rename(), remove(), mkdir(), makeConsistent()). mount() and format()
 * always start with empty caches. littlefs alone does not allow this, for two reasons that the binding handles:
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
 * time, with no other file open for writing, and rename() only within one directory (which is why they now expect the
 * poisoned mount after a failed rename()). The two situations outside of them are exactly the two of the poisoned
 * mount.
 *
 * @par Poisoned mount
 * Two situations leave littlefs with state in memory that no longer matches the medium, and the binding cannot
 * inspect or repair it. Both behave the same with littlefs v2.11.3 as with v2.9.3, so the pin does not change this:
 * - A failed rename() or makeConsistent(). When the old and the new entry are in different metadata pairs a rename()
 *   commits twice, and a read that fails in between leaves littlefs with a pending move. A repeat of the rename can
 *   then report success and later commits lose entries at the next mount, or trip an assertion of littlefs
 *   (`dir->count > 0` in lfs_dir_relocatingcommit()), which continues with a wrong count when assertions are compiled
 *   out. The pair is not the directory: a directory that has grown over several pairs (measured with 4 KiB blocks:
 *   from 65 entries with 30 character names to 119 with 3 character names) takes this path for a rename inside it,
 *   and the binding cannot ask which pair a path lands in. So every failed rename() poisons the mount, whatever its
 *   paths are. makeConsistent() commits the second half of such a rename (it deletes the moved entry) with the
 *   pending move already cleared in memory, so a failed commit leaves the same state, and every failure of it poisons
 *   the mount, whatever else is open. Without that, 1 of the 20 failing device calls of the repair on the volume of
 *   TestLittleFsConsistency.cpp lost a file at the next mount.
 * - A failed change (open for writing, File::write(), File::sync(), File::close(), remove(), mkdir(), and rename()
 *   as above) while another file is open for writing. One read position was observed to lose the data of that file at
 *   its close(), although the close reports success (issue #15).
 *
 * The binding then poisons the mount: every call is refused with Error::NeedsRemount until unmount() and mount().
 * - Refused are all methods and all File operations, including the retry of the failed call, open() and stat()
 *   (which read only), and the operations on the handles that were open. mount() is refused as well, because the
 *   file system is mounted; it does not clear the state. format() fails with Error::Busy like on any mounted file
 *   system. isMounted() stays true. File::close() reports Error::NeedsRemount, and its handle is invalid afterwards
 *   like after any close(), but the file slot is only released by unmount().
 * - unmount() is safe and returns success. It does not commit anything, in particular it does not close the writers
 *   through littlefs: it releases the file slots and unmounts. Every File that was open becomes stale
 *   (Error::BadHandle), and what a writer has not synced is lost. The destructor does the same. The same rule applies
 *   inside a healthy unmount(), which closes the writers in turn: if the close of one fails, that poisons the mount
 *   (the others are still open), the writers behind it are dropped without a commit, and unmount() returns that error.
 * - The state is per mount. unmount() ends it, and the next mount() starts from the medium like after a power cut.
 * - A failure that littlefs reports before it changed anything does not poison the mount: NoEntry, Exists,
 *   NotDirectory, IsDirectory, NotEmpty and InvalidArgument (which littlefs gives a rename of the root or onto it;
 *   and the errors that the binding reports itself, such as Busy or NameTooLong). Every other failure of the calls
 *   above poisons the mount in the two situations, including NoSpace and Corrupt. Anywhere else it does not: not with
 *   no other file open for writing (rename() and makeConsistent() excepted), and not for a failed read (File::read(),
 *   File::seek(), stat(), visitDirectory(), freeSpace(), open() for reading) with a writer open, which cost the writer
 *   no data in the tests.
 *
 * What a remount finds (TestLittleFsPoison.cpp fails a read, or a read, program or erase, at every position of the
 * call, and remounts at once):
 * - The volume is consistent and takes writes. A call that was interrupted has taken effect completely or not at all,
 *   and no other file changed. That was checked at 3516 failing positions of eleven calls with a writer open (a new
 *   file, and an existing one that was truncated or appended to, each with and without a sync in between), at 1700
 *   failing positions of eight renames, and at 1538 failing positions of two renames inside a directory of 80 entries
 *   that spans several metadata pairs (the shape a path comparison would have missed: 2 of 820 positions lost files
 *   of that directory when the caller went on instead of remounting).
 * - A writer keeps what it had synced, and nothing else: a new file exists and is empty, or holds the part written up
 *   to its last sync(); an existing file that was truncated by open() or appended to holds the content it had before
 *   it was opened (plus the synced part for Append). Everything written since the last sync is lost, exactly like
 *   after a power cut.
 * - Interrupted rename(): the volume is consistent, but littlefs applies its pending move in the first call that
 *   changes the volume, not at mount(). Until then a read can miss entries of the directory the rename moved from, also
 *   ones that have nothing to do with it: at 23 of 144 read positions of rename("/old", "/dir/moved") in the volume of
 *   TestLittleFsConsistency.cpp, visitDirectory("/") and stat() did not find an unrelated file next to the moved one
 *   (an upstream regression of littlefs, see "Consistency after a remount"). This is medium state, so it survives any
 *   number of read-only mounts. Call makeConsistent() after the remount and before reading anything that matters.
 *
 * @par Consistency after a remount
 * littlefs completes some changes lazily. A rename() between two metadata pairs commits twice, and between the two
 * commits the medium holds a pending move that hides the old entry. littlefs finishes it in the first call that
 * changes the volume after the next mount, never in mount() or in a read. The state is on the medium after a power cut
 * between the two commits, after an unclean power-down in general, and after a rename() that failed (see "Poisoned
 * mount"); it survives any number of read-only mounts. The unit is the metadata pair, not the directory: a rename()
 * inside one directory is affected as well when that directory has outgrown one pair (with 4 KiB blocks, from 65
 * entries with 30 character names to 119 with 3 character names), so this is not limited to renames into another
 * directory.
 *
 * Until the move is finished, the entry that follows the moved one in the source directory is missing: visitDirectory()
 * does not list it, and stat(), open() for reading and readFile() report Error::NoEntry for it, although the file is
 * intact. That is an upstream regression of littlefs (commit ddbfcaa7, in v2.9.1 up to and including v2.11.3), and it
 * hits unrelated files of that directory, for example the spool or backlog directory a task lists at boot. The
 * lookup by name is affected, not only the listing: a task that opens a record by name is told it does not exist,
 * and may drop it.
 *
 * makeConsistent() finishes the pending changes on request. The application must call it once after every mount()
 * that follows a NeedsRemount recovery or an unclean power-down (in practice after every boot), before it lists, opens,
 * reads or stat()s anything that matters. Before that call the volume is consistent but reads can be wrong.
 * - It is not part of mount(), on purpose. A mount that writes could fail with Io or NoSpace on exactly the failing
 *   medium that needs the repair, and the read-only recovery path would be gone. The application decides when a write
 *   is acceptable.
 * - On a consistent volume it reads and writes nothing (littlefs checks a flag in memory first).
 * - It is refused with Error::NotMounted or Error::NeedsRemount like every call.
 * - Open files are no obstacle: littlefs runs the same code before every remove(), mkdir(), rename() and open() for
 *   writing, with the open files on its list, so nothing extra is at stake. An open writer keeps its unsynced data.
 * - Cost when something is pending. The pending move of a rename costs about 20 device operations and one write
 *   (TestLittleFsConsistency.cpp). That is not a bound: an orphan (a metadata pair that no directory references, left
 *   by a power cut while a directory was removed, or while a block was relocated) makes littlefs traverse the whole
 *   metadata tree twice. That grows faster than linearly with the number of metadata pairs (measured with
 *   directories added to a volume: 33 device operations with none, 296 with 6, 1420 with 20, 7699 with 50), while
 *   the writes stay at 2 or 3, so it is a boot time cost, not wear. The test volume needs 570 operations and
 *   3 writes. Only the pending move, and the orphan of a removed directory on a small volume, are tested.
 * - A failure poisons the mount (see "Poisoned mount"), whatever else is open: littlefs deletes the moved entry with
 *   the move already cleared in memory, so a failed commit leaves the memory out of step with the medium like a failed
 *   rename(). The medium is still consistent afterwards.
 *
 * Boot sequence. The repair writes, so it can keep failing on a medium that cannot be written (a write that fails for
 * good, or NoSpace on a full volume). Every failure poisons the mount and every read is refused then, but the volume
 * itself stays readable after a remount without the repair. So do not retry in a loop:
 * 1. mount().
 * 2. makeConsistent(), once.
 * 3. If it failed: unmount() and mount() again, go on read-only without the repair, and accept that the entry behind a
 *    moved one can be hidden (also from open() and stat()) until a later attempt succeeds. Try the repair again on the
 *    next boot, or when space was freed or the medium works again, never in a tight loop.
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
      : LittleFsFileSystemBase(device, buffersOf(*this)) {
   }

   ~LittleFsFileSystem() override {
      shutdown();
   }

   private:
   /// Static, because it runs before the base is constructed: it takes only the addresses of the arrays of `self`.
   static Buffers buffersOf(LittleFsFileSystem &self) {
      Buffers buffers{};
      buffers.readCache = self.readCache_;
      buffers.programCache = self.programCache_;
      buffers.lookahead = self.lookahead_;
      buffers.slots = self.slots_;
      buffers.fileCaches = self.fileCaches_;
      return buffers;
   }

   std::array<uint8_t, CacheSize> readCache_{};
   std::array<uint8_t, CacheSize> programCache_{};
   std::array<uint8_t, LookaheadSize> lookahead_{};
   std::array<FileSlot, MaxOpenFiles> slots_{};
   std::array<uint8_t, MaxOpenFiles * CacheSize> fileCaches_{};
};

} // namespace art::storage

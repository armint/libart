#pragma once

#include "storage/BlockDevice.h"
#include "storage/File.h"
#include "storage/StorageError.h"
#include <cstddef>
#include <cstdint>
#include <etl/delegate.h>
#include <expected>
#include <span>

namespace art::storage {

/// How open() treats an existing or missing file, and which operations the returned File allows.
enum class OpenMode : uint8_t {
   Read,      ///< Read only. The file must exist (Error::NoEntry otherwise). Position 0.
   Write,     ///< Write only. Creates the file, or truncates it to size 0 if it exists. Position 0.
   Append,    ///< Write only. Creates the file if missing, keeps its content, every write goes to the end.
   ReadWrite, ///< Read and write. Creates the file if missing, keeps its content. Position 0.
};

/// Kind of a directory entry.
enum class EntryType : uint8_t {
   File,      ///< A regular file.
   Directory, ///< A directory.
};

/// What stat() and visitDirectory() report about an entry.
struct FileInfo {
   EntryType type{ EntryType::File };
   /// Size in bytes. Always 0 for a directory.
   offset_t size{ 0u };
};

/**
 * @brief Callback of FileSystem::visitDirectory(), called once per entry.
 *
 * The name is the entry's own name without the directory, null-terminated, and only valid during the call. The
 * entries "." and ".." are never reported. Return true to continue with the next entry and false to stop.
 * The callback runs while the file system holds its lock: it must not call back into the FileSystem or any
 * File of it.
 */
using DirectoryVisitor = etl::delegate<bool(const char *name, const FileInfo &info)>;

/**
 * @brief Abstract file system: what an application talks to instead of LittleFS, FAT or any other backend.
 *
 * Consumers depend only on this header. A second implementation (LittleFsFileSystem, a FAT adapter, a test
 * fake) plugs in without touching a consumer. The interface pulls in no file system implementation and
 * allocates nothing.
 *
 * Two styles are offered on purpose. The whole-file helpers readFile() and writeFile() keep simple consumers
 * short. The File handle returned by open() is seekable, which is what a FUOTA fragment writer needs to write
 * fragments into the middle of a staged image without knowing a physical flash address.
 *
 * @par Paths
 * Paths are null-terminated, absolute and use '/' as separator, for example "/fuota/image.bin". "/" is the
 * root directory. A path must not be null. Names and paths that exceed the limit of the implementation
 * fail with Error::NameTooLong. There is no working directory and there are no relative paths.
 *
 * @par Errors
 * Every fallible call returns `std::expected<T, Error>` with the codes of StorageError.h; backend specific
 * codes never escape the implementation. Calls other than mount() and format() fail with Error::NotMounted
 * while the file system is not mounted.
 *
 * @par Serialization contract
 * The implementation serializes; callers need not. Unlike BlockDevice, whose users must serialize all calls,
 * a FileSystem is safe to call from several tasks at once: every public method and every file*() hook
 * (that is, every File operation) takes the implementation's lock, so the operations of different tasks are
 * mutually exclusive and each is atomic with respect to the others. A backend that is not re-entrant, such as
 * LittleFS, is thereby hidden from consumers. The consequence is that no callback may run under the
 * lock and call back in: see DirectoryVisitor.
 *
 * @par Lifetime
 * A FileSystem is neither copyable nor movable, because Files point to it. It must outlive all its Files.
 */
class FileSystem {
   public:
   virtual ~FileSystem() = default;

   FileSystem(const FileSystem &) = delete;
   FileSystem &operator=(const FileSystem &) = delete;
   FileSystem(FileSystem &&) = delete;
   FileSystem &operator=(FileSystem &&) = delete;

   /**
    * @brief Mounts the file system found on the underlying medium.
    *
    * Mounting an already mounted file system succeeds and changes nothing. What happens to a medium without a
    * valid file system is up to the implementation: it either fails with Error::Corrupt or formats it (see
    * the documentation of the implementation). A failure never aborts the caller.
    */
   virtual std::expected<void, Error> mount() = 0;

   /**
    * @brief Unmounts the file system.
    *
    * All open files are flushed and closed first, and the Files that refer to them become stale: their
    * operations return Error::BadHandle. Unmounting a file system that is not mounted succeeds.
    *
    * @return Nothing on success, otherwise the error of flushing (for example Error::Io). The file system is
    * unmounted in every case.
    */
   virtual std::expected<void, Error> unmount() = 0;

   /**
    * @brief Creates an empty file system on the medium, destroying its previous content.
    *
    * The file system must not be mounted (Error::Busy otherwise) and stays unmounted; call mount() next.
    */
   virtual std::expected<void, Error> format() = 0;

   /// True between a successful mount() and the next unmount().
   [[nodiscard]] virtual bool isMounted() const = 0;

   /**
    * @brief Opens a file.
    *
    * @param path Absolute path of the file. Its directory must exist.
    * @param mode Creation and access mode, see OpenMode.
    * @return The open File, or Error::NoEntry (missing file or directory), Error::IsDirectory (the path is
    * a directory), Error::NotDirectory (a path component is a file), Error::NoMemory (all file slots are in
    * use), Error::NoSpace (a file that must be created cannot be) or another error.
    */
   virtual std::expected<File, Error> open(const char *path, OpenMode mode) = 0;

   /**
    * @brief Removes a file or an empty directory.
    *
    * Error::NoEntry if it does not exist, Error::NotEmpty for a directory with entries. Removing a file that
    * is open is not supported and the outcome is up to the implementation; close it first.
    */
   virtual std::expected<void, Error> remove(const char *path) = 0;

   /**
    * @brief Renames or moves a file or directory.
    *
    * If `newPath` is an existing file and `oldPath` is a file, the target is replaced. This is what the
    * "write a temporary file, then rename it over the old one" save pattern relies on; whether the
    * replacement is atomic across a power loss is up to the implementation (it is on LittleFS). Otherwise an
    * existing target is an error (Error::Exists, or Error::IsDirectory / Error::NotEmpty / Error::NotDirectory
    * when a directory is involved). Renaming an open file is not supported; close it first.
    */
   virtual std::expected<void, Error> rename(const char *oldPath, const char *newPath) = 0;

   /// Creates a directory. Its parent must exist. Error::Exists if the path is already taken.
   virtual std::expected<void, Error> mkdir(const char *path) = 0;

   /// Reports type and size of a file or directory. Error::NoEntry if it does not exist.
   virtual std::expected<FileInfo, Error> stat(const char *path) = 0;

   /**
    * @brief Calls `visitor` for every entry of a directory, in an unspecified order.
    *
    * A visitor is used instead of a directory handle: it needs no handle pool, and every known caller is a
    * streaming reduction (count, sum, find the oldest). Modifying the directory during the walk is not
    * allowed, which the rule that the visitor must not call back into the file system already forbids.
    *
    * @param path Absolute path of a directory (Error::NotDirectory otherwise, Error::NoEntry if missing).
    * @param visitor Called per entry, see DirectoryVisitor.
    * @return The number of entries the visitor was called for, including the one it stopped at.
    */
   virtual std::expected<size_t, Error> visitDirectory(const char *path, DirectoryVisitor visitor) = 0;

   /**
    * @brief Reports the free space of the file system in bytes.
    *
    * This is an estimate for capacity planning, not a promise that a write of that size succeeds: metadata,
    * block granularity and wear levelling all take their share. On some backends computing it is costly.
    */
   virtual std::expected<offset_t, Error> freeSpace() = 0;

   /**
    * @brief Reads a whole file into `buffer`.
    *
    * Written once in terms of open() and the File API, so it behaves identically on every implementation.
    *
    * @param path Absolute path of the file.
    * @param buffer Destination. It must be at least as large as the file.
    * @return The number of bytes read, which is the file size. If the file is larger than the buffer nothing
    * is read and the result is Error::NoMemory: a truncated read would be silently wrong. Otherwise the
    * error of open() or read().
    */
   std::expected<size_t, Error> readFile(const char *path, std::span<uint8_t> buffer);

   /**
    * @brief Creates or replaces a file with the given content and reports whether it was stored.
    *
    * Written once in terms of open() and the File API. The file is opened with OpenMode::Write, written and
    * closed, and the result of close() is returned, so a failed final flush is not lost.
    *
    * The replacement is not atomic at this level: a failure or power loss part way can leave the file
    * truncated or partially written. A caller that must never lose the old content writes a temporary file
    * and rename()s it over the target.
    */
   std::expected<void, Error> writeFile(const char *path, std::span<const uint8_t> data);

   protected:
   FileSystem() = default;

   /**
    * @name File hooks
    * File forwards its operations to these. Their contracts are those of the File methods of the same name.
    * An implementation must:
    * - take its lock in every hook, like in every public method (see the serialization contract);
    * - accept any handle value, including a stale or never issued one, and return Error::BadHandle for it
    *   instead of touching memory;
    * - release the handle in fileClose() whatever the outcome, and reject a second fileClose() of the same
    *   handle with Error::BadHandle. File relies on both.
    * @{
    */
   virtual std::expected<size_t, Error> fileRead(FileHandle handle, std::span<uint8_t> buffer) = 0;
   virtual std::expected<void, Error> fileWrite(FileHandle handle, std::span<const uint8_t> data) = 0;
   virtual std::expected<void, Error> fileSeek(FileHandle handle, offset_t position) = 0;
   virtual std::expected<offset_t, Error> fileTell(FileHandle handle) = 0;
   virtual std::expected<offset_t, Error> fileSize(FileHandle handle) = 0;
   virtual std::expected<void, Error> fileSync(FileHandle handle) = 0;
   virtual std::expected<void, Error> fileClose(FileHandle handle) = 0;
   /// @}

   /// Wraps a handle from the implementation's pool into the File that open() returns. Only an
   /// implementation can create a File that refers to a live handle.
   File makeFile(FileHandle handle) {
      return File{ this, handle };
   }

   private:
   friend class File;
};

} // namespace art::storage

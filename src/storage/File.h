#pragma once

#include "storage/StorageError.h"
#include "storage/StorageTypes.h"
#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include <cstddef>
#include <cstdint>

namespace art::storage {

class FileSystem;

/**
 * @brief Opaque identifier of an open file inside one FileSystem implementation.
 *
 * A FileHandle is a pool index, not a pointer. The pool lives in the implementation, which is what lets it
 * report a stale handle (closed, invalidated by unmount(), or reused) as Error::BadHandle instead of
 * dereferencing it. An implementation is free to encode more than the index, for example a generation
 * counter in the upper bits, so that a slot that was closed and reopened is told apart from the old handle.
 * Application code never creates or interprets a FileHandle; only File and FileSystem implementations do.
 */
using FileHandle = uint32_t;

/**
 * @brief Move-only RAII handle to an open file of a FileSystem.
 *
 * A File is what FileSystem::open() returns. It is non-virtual and two words wide (a FileSystem pointer and a
 * FileHandle), so it is returned by value and allocates nothing. It only forwards to the protected file*()
 * hooks of the FileSystem that created it and therefore behaves the same on every implementation.
 *
 * @par Ownership
 * The File owns the open file. It is closed when the File is destroyed, move-assigned over, or close()d. A
 * moved-from File, a default-constructed File and a closed File are all inert: every operation on them
 * fails with Error::BadHandle. The File does not own the FileSystem; the FileSystem must outlive every File
 * it handed out.
 *
 * @par Closing and errors
 * The destructor closes the file silently, because a destructor cannot report anything. A caller that cares
 * whether the final flush reached the medium calls close() and reads the result. Losing a write error at
 * destruction time is the classic file handle bug: data written to a file is only known to be stored once
 * close() (or sync()) returned success.
 *
 * @par Thread-safety
 * The FileSystem implementation serializes the operations of all its files with each other and with its own
 * methods (see FileSystem), so Files of different tasks need no external lock. A single File object is not
 * synchronized: do not use, move or destroy one File from two tasks at the same time.
 *
 * @par Positions
 * A file has one position, shared by read() and write(), starting at 0 (at the end of the file for
 * OpenMode::Append). Positions and sizes are byte counts of type offset_t.
 */
class File {
   public:
   /// Creates a File that is not open. Every operation on it returns Error::BadHandle.
   File() = default;

   File(const File &) = delete;
   File &operator=(const File &) = delete;

   /// Takes over the open file of `other`, which is left inert.
   File(File &&other) noexcept
      : fileSystem_(other.fileSystem_)
      , handle_(other.handle_) {
      other.fileSystem_ = nullptr;
   }

   /// Closes the file this File currently holds (silently), then takes over the one of `other`.
   File &operator=(File &&other) noexcept {
      if(this != &other) {
         closeSilently();
         fileSystem_ = other.fileSystem_;
         handle_ = other.handle_;
         other.fileSystem_ = nullptr;
      }
      return *this;
   }

   /// Closes the file, ignoring errors. Call close() to observe them.
   ~File() {
      closeSilently();
   }

   /// True while the File refers to an open file that the file system still knows. It is false for an inert
   /// File and for one whose handle became stale (unmount()). It asks the file system, so it takes its lock and
   /// is not free; do not call it in a loop.
   [[nodiscard]] bool isOpen() const;

   /**
    * @brief Reads from the current position and advances it by the number of bytes read.
    *
    * @param buffer Destination. At most buffer.size() bytes are read.
    * @return The number of bytes read. It is smaller than buffer.size() only when the end of the file was
    * reached, and 0 at or beyond the end. Otherwise the error, for example Error::InvalidArgument if the file
    * was opened write-only. The buffer content is unspecified after an error.
    */
   art::expected<size_t, Error> read(art::span<uint8_t> buffer);

   /**
    * @brief Writes at the current position (at the end for OpenMode::Append) and advances it.
    *
    * All of `data` is written or an error is returned; there are no short writes. After an error the file
    * content from the position onwards is unspecified. Data may be buffered: it is only known to be stored
    * after sync() or close() succeeded. Writing past the end extends the file; a gap between the old end and
    * the position (after seek()) reads back with unspecified content.
    *
    * @param data Source.
    * @return Nothing on success, otherwise the error, for example Error::NoSpace, or Error::InvalidArgument if
    * the file was opened read-only.
    */
   art::expected<void, Error> write(art::span<const uint8_t> data);

   /**
    * @brief Moves the position to an absolute byte offset from the start of the file.
    *
    * The offset may lie beyond the end of the file. That is not an error by itself: a read there returns 0
    * bytes and a write extends the file. To position relative to the end, seek to size() minus the distance.
    * A seek does not change the file size.
    *
    * @param position New position in bytes.
    * @return Nothing on success, otherwise the error, for example Error::TooBig if the file system cannot
    * represent the position.
    */
   art::expected<void, Error> seek(offset_t position);

   /// Returns the current position in bytes.
   art::expected<offset_t, Error> tell() const;

   /// Returns the size of the file in bytes, including data that was written but not yet synced.
   art::expected<offset_t, Error> size() const;

   /**
    * @brief Flushes buffered data of this file to the medium without closing it.
    *
    * @return Nothing on success, otherwise the error. After a failure the data since the last successful sync
    * may be lost.
    */
   art::expected<void, Error> sync();

   /**
    * @brief Flushes and closes the file, and reports whether that succeeded.
    *
    * The File is inert afterwards whatever the result: the underlying handle is released even when the final
    * flush failed, exactly like close(2), so a failed close cannot be retried and the destructor will not
    * try again. Closing a File that is not open returns Error::BadHandle.
    *
    * @return Nothing on success, otherwise the error of the final flush (for example Error::Io or
    * Error::NoSpace) or Error::BadHandle.
    */
   art::expected<void, Error> close();

   private:
   friend class FileSystem;

   File(FileSystem *fileSystem, FileHandle handle)
      : fileSystem_(fileSystem)
      , handle_(handle) {}

   void closeSilently();

   FileSystem *fileSystem_{ nullptr };
   FileHandle handle_{ 0u };
};

static_assert(sizeof(File) <= 2u * sizeof(void *), "File must stay two words wide");

} // namespace art::storage

#pragma once

#include "storage/File.h"
#include "streams/BoundedWritable.h"
#include <limits>
#include <optional>

/**
 * A BoundedWritable over an art::storage::File, so that the stream utilities (BufferedWritable,
 * DigestWritable, the protobuf codecs, ...) can write to a file of any FileSystem implementation.
 *
 * The adapter holds no buffer of its own; every write() is one File::write(). To batch small writes into
 * larger ones wrap it in a BufferedWritable, but see "Flushing" below. The stream writes at the current position of the file (at the end
 * for OpenMode::Append) and tracks nothing else, so the file may be seeked and closed through its File.
 *
 * @par All or nothing
 * Writable::write() reports the number of bytes written, but File::write() is all-or-error: it returns
 * nothing on success and has no short write. The adapter therefore returns data.size() on success and an
 * error otherwise, and never a partial count. After an error the file content from the position onwards is
 * unspecified, as documented for File::write().
 *
 * @par Errors
 * A File error is reported as the existing Writable::Error vocabulary: art::storage::Error::NoSpace and
 * art::storage::Error::TooBig become Writable::Overflow (nothing more fits), every other error becomes
 * Writable::CouldNotWrite. The cause is kept in lastError().
 *
 * @par Flushing
 * Neither FileWritable nor BufferedWritable flushes in its destructor, so data still held in a BufferedWritable
 * is dropped without an error when it goes out of scope. The caller must call flush() on the outermost writable
 * explicitly. This differs from FatFileWritable, whose destructor flushes its buffer. FileWritable does not
 * buffer, so it has nothing to flush there, and a destructor could not report a failure anyway.
 *
 * @par Durability
 * Written data may be buffered by the file system. flush() calls File::sync() to store it, and close() does
 * the same and reports the result. This holds only while every write() succeeded: after a failed write() the
 * content from the position onwards is unspecified, and a later successful flush() does not make it valid.
 * Treat a failed write as a failed file.
 */
class FileWritable : public BoundedWritable {
   art::storage::File &file_;
   std::optional<art::storage::Error> lastError_;

   public:
   /** @param file the open file to write to; it must outlive the stream */
   explicit FileWritable(art::storage::File &file)
      : BoundedWritable(std::numeric_limits<size_t>::max())
      , file_(file) {}

   // Not movable, like FileReadable.
   FileWritable(FileWritable &&) = delete;
   FileWritable &operator=(FileWritable &&) = delete;
   FileWritable(const FileWritable &) = delete;
   FileWritable &operator=(const FileWritable &) = delete;

   /** The Writable::Error a File error is reported as. */
   static constexpr Writable::Error toWritableError(art::storage::Error error) {
      using art::storage::Error;
      return (error == Error::NoSpace || error == Error::TooBig) ? Writable::Overflow : Writable::CouldNotWrite;
   }

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override;

   /** Syncs the file to the medium (File::sync()). */
   std::expected<void, Error> flush() override;

   /**
    * Moves the write position, in bytes from the start of the file. On failure the position is left
    * unchanged and the cause is available in lastError().
    */
   void reset(size_t position = 0) override;

   /** The write position in bytes from the start of the file. 0 if the file cannot report its position. */
   size_t position() override;

   /** The cause of the most recent File error, kept until the stream is destroyed. Empty if none occurred. */
   [[nodiscard]] std::optional<art::storage::Error> lastError() const {
      return lastError_;
   }
};

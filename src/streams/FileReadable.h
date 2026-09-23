#pragma once

#include "storage/File.h"
#include "streams/RandomAccessReadable.h"
#include <optional>
#include <span>

/**
 * A RandomAccessReadable over an art::storage::File, so that the stream utilities (DigestReadable,
 * CharacterSkippingReadable, the protobuf codecs, ...) work on a file of any FileSystem implementation.
 *
 * Every File::read() goes through the file system, which takes its lock, so reading one byte per call would be
 * slow. The caller therefore provides a buffer; the stream fills it with one File::read() and serves read()
 * from it. Readable::read(std::span) is not virtual and reads byte by byte, so the buffer is what makes bulk
 * reads efficient. The buffer must stay valid for the lifetime of the stream, and the file must not be read,
 * written or seeked through another handle while the stream is in use, because the stream tracks the file
 * position. It starts reading at the current position of the file.
 *
 * @par Errors
 * A File error is reported as the existing Readable::Error vocabulary: art::storage::Error::Timeout becomes
 * Readable::Timeout, every other error becomes Readable::ReadError, and the end of the file is
 * Readable::EndOfStream. The distinction that Readable::Error cannot carry (NoEntry, Io, Corrupt, BadHandle, ...)
 * is kept in lastError().
 */
class FileReadable : public RandomAccessReadable {
   art::storage::File &file_;
   std::span<uint8_t> buffer_;
   size_t bufferIndex_ = 0;
   size_t bytesBuffered_ = 0;
   std::optional<art::storage::Error> lastError_;

   /// Reads the file position and size; on failure records the cause and returns false.
   bool fileExtent(art::storage::offset_t &position, art::storage::offset_t &size);
   /// File offset of the first byte in the buffer, given the current file position.
   art::storage::offset_t bufferStart(art::storage::offset_t filePosition) const;

   public:
   /**
    * @param file the open file to read from; it must outlive the stream
    * @param buffer read buffer; an empty buffer makes every read() fail with ReadError
    */
   FileReadable(art::storage::File &file, std::span<uint8_t> buffer)
      : file_(file)
      , buffer_(buffer) {}

   // Not movable: a moved-from stream would stay live on the same file and position.
   FileReadable(FileReadable &&) = delete;
   FileReadable &operator=(FileReadable &&) = delete;
   FileReadable(const FileReadable &) = delete;
   FileReadable &operator=(const FileReadable &) = delete;

   /** The Readable::Error a File error is reported as. */
   static constexpr Readable::Error toReadableError(art::storage::Error error) {
      return error == art::storage::Error::Timeout ? Readable::Timeout : Readable::ReadError;
   }

   using RandomAccessReadable::read;
   std::expected<char, Error> read() override;

   /**
    * True if read() returns a byte: buffered data is left, or the file has data beyond its position. False at
    * the end of the file and also when the file fails; the failure is recorded in lastError().
    */
   bool available() override;

   /** Bytes left to read, buffered and in the file. Only the buffered part if the file fails; see lastError(). */
   size_t bytesLeft() override;

   /**
    * Moves the read position, in bytes from the start of the file. A position within the buffer is served
    * without touching the file. A position beyond the end is allowed; the next read() reports EndOfStream.
    * On failure the position is left unchanged and the cause is available in lastError().
    */
   void reset(size_t position = 0) override;

   /** The read position in bytes from the start of the file. 0 if the file cannot report its position. */
   size_t position() override;

   /** The cause of the most recent File error, kept until the stream is destroyed. Empty if none occurred. */
   [[nodiscard]] std::optional<art::storage::Error> lastError() const {
      return lastError_;
   }
};

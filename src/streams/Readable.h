#pragma once

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <span>

#include <art_types.h>
#include <utils/endian.h>

/**
 * Interface for a readable source of data, providing various methods to read and process characters or data
 * from the stream. All derived classes are expected to implement this interface as appropriate.
 */
class Readable {
   protected:
   uint32_t timeout_ = 0;

   public:
   using Error = int32_t;
   enum { EndOfStream = 0, ReadError, Timeout, NotFound, NumberTooBig, EndOfMessage, Break };

   /**
    * Read a single character from this stream. This method should be implemented by all derived classes
    *
    * @return An std::expected<char, Error> containing the read character, or an error on failure
    */
   virtual std::expected<char, Error> read() = 0;

   std::expected<size_t, Error> read(std::span<char> target) {
      auto bytes = std::span(reinterpret_cast<uint8_t *>(target.data()), target.size());
      return read(bytes);
   }

   std::expected<size_t, Error> read(std::span<uint8_t> target);

   /** Read a number from the readable, converting endianness if requested
    *  \tparam T the type to read
    *  \tparam E the expected endianness of the incoming bytes
    *  \return an std::expected containing the read value, or a Readable::Error if something went wrong
    */
   template <typename T, Endian E = SystemEndian>
   std::expected<T, Error> readNumber() {
      static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
      uint8_t buffer[sizeof(T)];
      auto r = read(buffer);
      if(!r) {
         return r;
      }
      T tmp{};
      std::memcpy(&tmp, buffer, sizeof(T));
      T val = convertEndian<E, T>(tmp);
      return val;
   }

   /**
    * Decodes a Variable Length Quantity (VLQ/LEB128) integer from this readable.
    * This project refers to this as Run-Length Encoding (RLE) for historical reasons.
    *
    * @tparam T integer type to decode into.
    * @param dest Reference to destination variable that will receive the decoded value on success.
    * @return The number of bytes consumed on success; otherwise a DecodeError.
    */
   template <typename T, typename = std::enable_if_t<std::is_integral_v<T>, T>>
   std::expected<size_t, Error> readRLE(T &dest) {
      using UnsignedT = std::make_unsigned_t<T>;
      UnsignedT uDest = 0;
      size_t index = 0;
      size_t bitIndex = 0;
      uint8_t byte;
      do {
         auto ch = read();
         if(!ch) {
            return ch;
         }
         byte = static_cast<uint8_t>(*ch);
         if(bitIndex >= sizeof(T) * 8) {
            return std::unexpected(NumberTooBig);
         }
         uDest |= static_cast<UnsignedT>(byte & 0x7F) << bitIndex;
         index++;
         bitIndex += 7;
      } while(byte & 0x80);
      dest = static_cast<T>(uDest);
      return index;
   }


   /**
    * Check if a read for this readable would result in blocking or an error
    *
    * @return true if the read() method returns immediately and without error, otherwise false
    */
   virtual bool available() = 0;

   /**
    * Read from this readable until one of the strings in toFind is matched or an error occurs
    *
    * @param toFind the list of strings that are to be found in this stream
    * @param buffer and optional buffer, every character that was read from the stream will be written in this buffer,
    *               until the buffer is full or  the method exits
    * @param charsRead an optional pointer to a size_t where the total number of characters read will be stored
    *
    * @return A std::expected<size_t, Error> containing the index of the matched string, or an error on failure
    */
   std::expected<size_t, Error>
   readUntil(std::initializer_list<const char *> toFind, std::span<char> buffer = std::span<char>(), size_t *charsRead = nullptr);

   /**
    * See C scanf for documentation.
    *
    * A format specifier for scanf follows this prototype:
    *
    * %[*][width][length]specifier
    *
    * Supported specifiers in this implementation:
    *
    * | Specifier    | Description                                 |
    * | :----------: | :------------------------------------------ |
    * | i, d         | Signed integer                              |
    * | u            | Unsigned integer                            |
    * | x, X         | Hexadecimal integer                         |
    * | p            | Hexadecimal integer, read into an uintptr_t |
    * | c            | Single character                            |
    * | s            | String of characters                        |
    * | %            | A literal %                                 |
    *
    * | Subspecifier | Description                                                                            |
    * | :----------: | :------------------------------------------------------------------------------------- |
    * | *            | Data indicated by the specifier is read but ignored                                    |
    * | width        | Specifies the maximum number of characters to be read in the current reading operation |
    * | length       | l (long), and ll (long long) are currently the only supported length specifiers        |
    *
    * @param format The format string
    * @param ...    The parameters to be read from the sting
    *
    * @return The number of matches read from the string and stored
    */
   std::expected<size_t, Error> scanf(const char *format, ...);
   std::expected<size_t, Error> vscanf(const char *format, va_list args);

   [[nodiscard]] uint32_t getTimeout() const {
      return timeout_;
   }

   void setTimeout(uint32_t timeout) {
      timeout_ = timeout;
   }

   Readable() = default;
   virtual ~Readable() = default;
   Readable(Readable&&) = default;
   Readable& operator=(Readable&&) = default;
   Readable(const Readable&) = default;
   Readable& operator=(const Readable&) = default;

   protected:
   std::expected<char, Error> scanfRead();
   /**
    * Reads a signed integer from this readable source.
    *
    * @param firstChar An optional initial character, if already read, to be included in the integer parse.
    *                  If not provided, the method will attempt to read the first character itself.
    * @param base      The numerical base (e.g., 10 for decimal, 16 for hexadecimal) used for reading the integer.
    * @param maxDigits Maximum number of digits to read for the integer.
    *
    * @return A std::expected<int64_t, Readable::Error> containing the read signed integer value, or an error on failure.
    */
   std::expected<big_int_t, Error>
   readSigned(std::optional<char> firstChar, int base, size_t maxDigits = std::numeric_limits<size_t>::max());
   /**
    * Reads an unsigned integer from the stream. The reading process starts with the provided first character
    * (if any), and continues parsing subsequent characters based on the specified base until either the maximum
    * number of digits has been read or a non-digit character is encountered.
    *
    * @param firstChar An optional initial character to begin parsing with. If not provided, the method starts by reading from the stream.
    * @param base The base to use for parsing the digits, which should be between 2 and 36 inclusive.
    * @param maxDigits The maximum number of digits to read.
    *
    * @return A std::expected containing the parsed unsigned integer if successful, or an error code of type Readable::Error.
    */
   std::expected<big_uint_t, Error>
   readUnsigned(std::optional<char> firstChar, int base, size_t maxDigits = std::numeric_limits<size_t>::max());
};

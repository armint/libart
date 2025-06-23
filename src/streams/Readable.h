#pragma once

#include <array>
#include <cstdarg>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>

/**
 * Interface for a readable source of data, providing various methods to read and process characters or data
 * from the stream. All derived classes are expected to implement this interface as appropriate.
 */
class Readable {
   public:
   using Error = int32_t;
   enum { EndOfStream = 0, ReadError, Timeout, NotFound };

   /**
    * Read a single character from this stream. This method should be implemented by all derived classes
    *
    * @return An std::expected<char, Error> containing the read character, or an error on failure
    */
   virtual std::expected<char, Error> read() = 0;

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

   virtual ~Readable() = default;

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
   std::expected<int64_t, Error>
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
   std::expected<uint64_t, Error>
   readUnsigned(std::optional<char> firstChar, int base, size_t maxDigits = std::numeric_limits<size_t>::max());
};

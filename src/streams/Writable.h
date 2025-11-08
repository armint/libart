
#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <utils/Timeout.h>
#include <utils/endian.h>

/**
 * Represents an interface or abstraction for writable resources or objects,
 * allowing data to be written to them.
 */
class Writable {
   public:
   using Error = int32_t;
   enum { CouldNotWrite = 0, Overflow };

   protected:
   struct TfpHelper {
      Writable* writable;
      std::expected<size_t, Error> result;
   };
   static void writeCharacter(void *ptr, char c);

   public:
   Writable() = default;
   virtual ~Writable() = default;

   /** Write an amount of data to this writable
    * \param data the data to write
    * \return an std::expected containing the amount of bytes written, or a Writable::Error if something went wrong
    */
   virtual std::expected<size_t, Error> write(std::span<const uint8_t> data) = 0;

   /** Write a number to the writable, converting endianness if requested
    * \tparam T the type to write
    * \tparam E the requested endianness
    * \param number the number to write
    * \return an std::expected containing the amount of bytes written, or a Writable::Error if something went wrong
    */
   template <typename T, Endian E = SystemEndian>
   inline std::expected<size_t, Error> writeNumber(T number) {
      const T tmpVal = convertEndian<E, T>(number);
      return write(std::span(reinterpret_cast<const uint8_t *>(&tmpVal), sizeof(T)));
   }

   /**
    * Write a single byte to this writable.
    * @param c the byte to write
    * @return an std::expected containing the amount of bytes written, or a Writable::Error if something went wrong
    */
   inline std::expected<size_t, Error> write(const uint8_t c) {
      return write(std::span<const uint8_t>(&c, 1));
   }

   /**
    * Printf for this writable. Internally, tfp_printf is used
    *
    * @param format the tfp_printf format
    * @param ...  the variable args for tfp_printf
    * @return an std::expected containing the amount of bytes written, or a Writable::Error if something went wrong
    */
   std::expected<size_t, Error> printf(const char *format, ...);

   std::expected<size_t, Error> vprintf(const char *format, va_list va_l);

   inline std::expected<size_t, Error> println() {
      return write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("\r\n"), 2));
   }
   std::expected<size_t, Error> println(const char *format, ...);

   /**
    * Flush possible buffered data of this writable.
    * @return An error in case the flush was not successful
    */
   virtual std::expected<void, Error> flush() = 0;
};

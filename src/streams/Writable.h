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
      Writable *writable;
      std::expected<size_t, Error> result;
   };

   static void writeCharacter(void *ptr, char c);

   /** Called when a WriteSession begins. Override to take a lock or start batching for the
    *  duration of the session. Default is a no-op. */
   virtual void beginWriteSession() noexcept {}

   /** Called when a WriteSession ends. Override to release a lock or flush batching started
    *  in beginWriteSession(). Default is a no-op. */
   virtual void endWriteSession() noexcept {}

   public:
   Writable() = default;
   virtual ~Writable() = default;
   Writable(Writable&&) = default;
   Writable& operator=(Writable&&) = default;
   Writable(const Writable&) = default;
   Writable& operator=(const Writable&) = default;

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
      static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
      const T tmpVal = convertEndian<E, T>(number);
      const auto data = reinterpret_cast<const uint8_t *>(&tmpVal);
      return write(std::span<const uint8_t>(data, sizeof(T)));
   }

   /**
    * Encodes a value using Variable Length Quantity (VLQ/LEB128) encoding and writes it to the writable.
    * This project refers to this as Run-Length Encoding (RLE) for historical reasons.
    * The encoding scheme compresses integer values into a series of 7-bit chunks,
    * with the highest bit of each chunk signaling continuation.
    *
    * @tparam T The type of the value to encode. Typically an integer type.
    * @param value The value to be encoded.
    * @return An `std::expected` containing the number of bytes written on success,
    *         or a `Writable::Error` code if writing fails.
    */
   template <typename T, typename = std::enable_if_t<std::is_integral_v<T>, T>>
   std::expected<size_t, Error> writeRLE(T value) {
      using UnsignedT = std::make_unsigned_t<T>;
      UnsignedT uValue = static_cast<UnsignedT>(value);
      size_t i = 0;
      do {
         uint8_t c;
         if(uValue > 0x7F) {
            c = static_cast<uint8_t>((uValue & 0x7F) | 0x80);
         } else {
            c = static_cast<uint8_t>(uValue);
         }
         auto res = write(c);
         if(!res) {
            return res;
         }
         uValue >>= 7;
         ++i;
      } while(uValue != 0);
      return i;
   }

   /**
    * Write a single byte to this writable.
    * @param c the byte to write
    * @return an std::expected containing the amount of bytes written, or a Writable::Error if something went wrong
    */
   inline std::expected<size_t, Error> write(const uint8_t c) {
      return write(std::span(&c, 1));
   }

   /**
    * RAII scope bracketing a sequence of write()/printf() calls, so a sink whose write()
    * does non-trivial per-call work (locking, context switches, ...) can do that work once
    * for the whole sequence instead of once per call.
    *
    * Obtained via writeSession(); the default beginWriteSession()/endWriteSession() hooks are
    * no-ops, so opening a session costs nothing for sinks that do not override them.
    * `vprintf()` opens one for the duration of a single format; callers such as
    * `logging::Logger` can hold a wider one across several printf()/write() calls (e.g. a log
    * line's prefix, body, suffix and flush) to make the whole sequence atomic from the sink's
    * point of view.
    *
    * Sessions may nest (an outer session held by a caller, with `vprintf()` opening its own
    * inner one for each formatted call within it). The base class does not track nesting
    * depth -- a sink whose begin/end hooks are not naturally reentrant (e.g. they take a lock)
    * must handle that itself, for instance with a recursive lock.
    */
   class WriteSession {
      public:
      explicit WriteSession(Writable &writable) noexcept
         : writable_(&writable) {
         writable_->beginWriteSession();
      }
      ~WriteSession() {
         if(writable_) {
            writable_->endWriteSession();
         }
      }

      WriteSession(const WriteSession &) = delete;
      WriteSession &operator=(const WriteSession &) = delete;
      WriteSession &operator=(WriteSession &&) = delete;

      WriteSession(WriteSession &&other) noexcept
         : writable_(other.writable_) {
         other.writable_ = nullptr;
      }

      private:
      Writable *writable_;
   };

   /** Open a WriteSession bracketing subsequent write()/printf() calls until the returned
    *  object is destroyed. See WriteSession. */
   [[nodiscard]] WriteSession writeSession() noexcept {
      return WriteSession(*this);
   }

   /**
    * Bufferless printf: directly streams characters via tfp_format into write().
    * This C++ template forwards to an internal C-ellipsis overload to build a va_list.
    */
   template <typename... Args>
   std::expected<size_t, Error> printf(const char *format, Args &&...args) {
      return printf_va(format, std::forward<Args>(args)...);
   }

   std::expected<size_t, Error> vprintf(const char *format, va_list va_l);

   // C-ellipsis helper used by the template to avoid intermediate buffers
   std::expected<size_t, Error> printf_va(const char *format, ...);

   inline std::expected<size_t, Error> println() {
      return write(std::span(reinterpret_cast<const uint8_t *>("\r\n"), 2));
   }

   template <typename... Args>
   std::expected<size_t, Error> println(const char *format, Args &&...args) {
      auto ret = printf(format, std::forward<Args>(args)...);
      if(!ret) {
         return ret;
      }
      auto r2 = write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("\r\n"), 2));
      if(!r2) {
         return r2;
      }
      ret.value() += r2.value();
      return ret;
   }

   /**
    * Flush possible buffered data of this writable.
    * @return An error in case the flush was not successful
    */
   virtual std::expected<void, Error> flush() = 0;
};
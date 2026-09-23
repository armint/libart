#pragma once

#include <cstdint>

namespace art::storage {

/**
 * @brief Error codes shared by the storage abstraction layers (block devices, file systems).
 *
 * The codes are deliberately independent of any concrete backend. Adapters translate their native error
 * values (LittleFS, FatFs, HAL status codes, ...) into this enum, so code above the adapter never depends
 * on a specific driver.
 *
 * Fallible storage operations return `std::expected<T, Error>`.
 */
enum class Error : uint8_t {
   Io,              ///< Low-level read, program or erase failure reported by the medium or its bus.
   Corrupt,         ///< On-medium structures are inconsistent or fail an integrity check.
   NoEntry,         ///< The file or directory does not exist.
   Exists,          ///< The file or directory already exists.
   NotDirectory,    ///< A directory was expected but the entry is not one.
   IsDirectory,     ///< A regular file was expected but the entry is a directory.
   NotEmpty,        ///< The directory is not empty.
   NameTooLong,     ///< A file name or path exceeds the limit of the file system.
   NoSpace,         ///< No free space is left on the medium.
   NoMemory,        ///< A caller-provided or static buffer is too small, or a static pool is exhausted.
   TooBig,          ///< The file or the requested size exceeds the maximum the file system supports.
   InvalidArgument, ///< A parameter is out of range, misaligned, or otherwise not acceptable.
   BadHandle,       ///< The file or directory handle is not valid (closed, moved-from, or never opened).
   NotMounted,      ///< The operation requires a mounted file system.
   NotInitialised,  ///< The device or file system was used before a successful `init()`.
   Unsupported,     ///< The operation is not supported by this device or backend.
};

/**
 * @brief Returns a short, static, human-readable name for an error code, intended for logging.
 *
 * @param error The error code to describe.
 * @return A null-terminated string with static storage duration; never `nullptr`. Values outside the
 * enumeration yield `"Unknown"`.
 */
constexpr const char *toString(Error error) {
   switch(error) {
   case Error::Io:
      return "Io";
   case Error::Corrupt:
      return "Corrupt";
   case Error::NoEntry:
      return "NoEntry";
   case Error::Exists:
      return "Exists";
   case Error::NotDirectory:
      return "NotDirectory";
   case Error::IsDirectory:
      return "IsDirectory";
   case Error::NotEmpty:
      return "NotEmpty";
   case Error::NameTooLong:
      return "NameTooLong";
   case Error::NoSpace:
      return "NoSpace";
   case Error::NoMemory:
      return "NoMemory";
   case Error::TooBig:
      return "TooBig";
   case Error::InvalidArgument:
      return "InvalidArgument";
   case Error::BadHandle:
      return "BadHandle";
   case Error::NotMounted:
      return "NotMounted";
   case Error::NotInitialised:
      return "NotInitialised";
   case Error::Unsupported:
      return "Unsupported";
   }
   return "Unknown";
}

} // namespace art::storage

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
 *
 * Adapters should keep the distinctions a caller can act on: NotReady, Timeout and Busy are transient and may
 * be retried or backed off, WriteProtected is permanent until the user intervenes, Io is a hard failure.
 * NeedsRemount is the one code where a retry is wrong: the file system holds state it cannot trust and refuses
 * everything until the caller unmounts and mounts it again (see LittleFsFileSystem).
 * Native codes without a dedicated enumerator map to the closest one. The FatFs mapping is:
 * - FR_DISK_ERR, FR_MKFS_ABORTED: Io (a failed format is a failed medium access; the cause is not observable).
 * - FR_NOT_READY: NotReady (no card in the slot, or not yet out of its power-up sequence).
 * - FR_TIMEOUT: Timeout. FR_LOCKED: Busy. FR_WRITE_PROTECTED: WriteProtected.
 *   The reverse direction is lossy: FatFs has no transient disk result, so the FAT disk layer (diskio.cpp) reports
 *   a device Timeout or Busy as RES_ERROR, and it comes back as FR_DISK_ERR, that is Io. Unlike LittleFsFileSystem,
 *   a FAT adapter therefore cannot preserve Timeout or Busy of the device below it.
 * - FR_INT_ERR, FR_NO_FILESYSTEM: Corrupt (no valid FAT volume on the medium).
 * - FR_NO_FILE, FR_NO_PATH: NoEntry. FR_EXIST: Exists.
 * - FR_DENIED is not one condition; FatFs returns it for unrelated causes, so map it per call site, never as
 *   a blanket code (line numbers refer to src/storage/fatfs/ff.c):
 *   - No free cluster or directory entry, or too many short-name collisions (dir_next, dir_register), no room
 *     for a new directory cluster (f_mkdir, :5514), no contiguous cluster block (f_expand, :6081, :6117):
 *     NoSpace.
 *   - f_expand precondition guard (:6065): size 0, file not empty, or handle without FA_WRITE: InvalidArgument.
 *     f_expand size limit on a non-exFAT volume (:6068): TooBig.
 *   - f_unlink: non-empty directory (:5461): NotEmpty. Current directory (:5445): Busy. Read-only object
 *     (:5424): InvalidArgument.
 *   - f_rename of a sub-directory in the current directory path (:5611): Busy.
 *   - Handle used against its access mode: f_read (:4286) and f_forward (:6180) without FA_READ, f_write (:4394)
 *     and f_truncate (:5363) without FA_WRITE: InvalidArgument. f_open with FA_WRITE on a read-only file
 *     (:4167): InvalidArgument.
 *   - f_open creating over an existing object (:4117) tests read-only file OR directory with the same code, so
 *     the adapter cannot tell them apart from the result. It maps to InvalidArgument (the path cannot be
 *     opened for create/overwrite); an adapter that must report IsDirectory has to stat the path first.
 * - FR_INVALID_NAME, FR_INVALID_DRIVE, FR_INVALID_PARAMETER: InvalidArgument. FR_INVALID_OBJECT: BadHandle.
 * - FR_NOT_ENABLED: NotMounted.
 * - FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES: NoMemory (a static buffer or pool is exhausted).
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
   Timeout,         ///< The device or a lock did not become ready in time. Retrying may succeed.
   Busy,            ///< The device or resource is in use or locked by someone else. Retrying later may succeed.
   WriteProtected,  ///< The medium or device is write protected (SD lock switch, NOR block protection).
   NotReady,     ///< No medium present or the device is not ready yet (empty SD slot, power-up). Retry or ask the user.
   NeedsRemount, ///< The file system refuses every call until unmount() and mount(). A retry is refused as well.
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
   case Error::Timeout:
      return "Timeout";
   case Error::Busy:
      return "Busy";
   case Error::WriteProtected:
      return "WriteProtected";
   case Error::NotReady:
      return "NotReady";
   case Error::NeedsRemount:
      return "NeedsRemount";
   }
   return "Unknown";
}

} // namespace art::storage

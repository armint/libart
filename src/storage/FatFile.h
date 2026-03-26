#pragma once
#include "fatfs/ff.h"
#include <expected>
#include <span>
#ifdef UNIT_TESTING
#include <cstdint>
#include <vector>
#endif

/**
 * @class FatFile
 *
 * @brief FatFile represents a file within a FAT filesystem.
 *
 * This class provides mechanisms to interact with files on a FAT
 * (File Allocation Table) filesystem. Users can perform operations
 * such as reading from and writing to a file, as well as managing
 * file metadata and attributes.
 *
 * The FatFile class acts as an interface to the underlying filesystem,
 * abstracting away the low-level details and providing an API for
 * common file operations.
 *
 * @details
 * This class typically interacts with lower-level functions of a FAT
 * filesystem driver, managing file position, size, and other attributes
 * that are essential for correct file manipulation.
 */
class FatFile {
   FIL _fil{};
   FRESULT _res;

   public:
#ifdef UNIT_TESTING
   struct MockState {
      std::vector<uint8_t> writtenData;
      std::vector<uint8_t> fileData;
      size_t currentPosition = 0;
      bool flushed = false;
      bool writeCalled = false;
      bool syncCalled = false;
      FRESULT nextReadResult = FR_OK;
   };
   static inline MockState *state = nullptr;
#endif
   /**
    * Constructor for the FatFile class.
    *
    * Initializes a FatFile object and opens the file specified by the path
    * with the mode provided.
    *
    * @param path The path to the file to be opened.
    * @param mode The mode in which to open the file. This could be a bitwise
    * combination of FA_READ, FA_WRITE, FA_CREATE_ALWAYS, etc.
    *
    * @return A FatFile instance representing the opened file.
    *         If the file cannot be opened, relevant error handling should
    *         be implemented in the class methods.
    */
   FatFile(const char *path, uint8_t mode);
   ~FatFile();
   /**
    * @brief Checks if the file exists.
    *
    * This method determines whether the file represented by the FatFile object
    * exists on the filesystem.
    *
    * @return True if the file exists, otherwise false.
    */
   inline bool exists() const {
      return _res == FR_OK;
   }
   /**
    * @brief Reads data from the file into the provided buffer.
    *
    * This method reads up to the size of the buffer from the file associated with the FatFile
    * object into the given buffer. It returns the number of bytes successfully read, or an
    * error code if the read operation fails.
    *
    * @param buffer The buffer where the read data will be stored. The buffer's size determines
    *               the maximum amount of data that will be read from the file.
    *
    * @return A std::expected object containing the number of bytes read on success, or an FRESULT
    *         indicating the type of error that occurred.
    */
   std::expected<size_t, FRESULT> read(std::span<uint8_t> buffer);
   /**
    * Writes data from a provided buffer to a file within the FAT filesystem.
    *
    * This method attempts to write the contents of the provided buffer to
    * the file associated with this FatFile object. The operation writes the
    * data sequentially, starting from the current file position. If the write
    * operation is successful, the file position is advanced by the number of
    * bytes written.
    *
    * @param buffer A span that contains the data to be written to the file.
    *               The span should point to a contiguous block of memory
    *               containing the data.
    *
    * @return std::expected<size_t, FRESULT> On success, returns the number of bytes
    *                                        actually written wrapped in an expected
    *                                        object. On failure, returns an unexpected
    *                                        object containing the FRESULT error code.
    */
   std::expected<size_t, FRESULT> write(std::span<const uint8_t> buffer);
   /**
    * @brief Synchronizes the file's in-memory state with the storage media.
    *
    * This function ensures that any buffered data for the file is written
    * to the storage device, ensuring data consistency. If an error occurs
    * during the synchronization process, the error will be returned.
    *
    * @return An expected<void, FRESULT> object where:
    * - `void` on success
    * - `FRESULT` on failure, indicating the specific error that occurred
    */
   std::expected<void, FRESULT> flush();
   /**
    * Seeks to a specific position within the file.
    *
    * This method moves the file pointer to the specified offset.
    * If the seek operation fails, an appropriate FAT filesystem
    * error code will be returned.
    *
    * @param offset The position, in bytes, to seek to within the file.
    * @return A std::expected<void, FRESULT> either holding no value on success or an FRESULT error code on failure.
    */
   std::expected<void, FRESULT> seek(size_t offset);
   /**
    * Retrieves the current file position.
    *
    * This method returns the current position within the file, represented as the number
    * of bytes from the beginning of the file.
    *
    * @return The current file position as a size_t value.
    */
   size_t tell();
   /**
    * @brief Retrieves the size of the file in bytes.
    *
    * This method provides the current size of the file based on its
    * allocation in the FAT filesystem. The size is determined by the
    * underlying filesystem structures and reflects the actual data
    * stored within the file.
    *
    * @return The size of the file in bytes.
    */
   size_t size();
};

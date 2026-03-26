#pragma once
#include "fatfs/ff.h"
#include <cstddef>

/**
 * @class FatDir
 * @brief A class for handling directory operations in a FAT filesystem.
 */
class FatDir {
   DIR _dir;
   FRESULT _res;

   public:
   /**
    * @brief Constructs a FatDir object and initializes the directory for the given path.
    *
    * @param path Path to the directory to be opened.
    *
    * @return None. The constructor initializes the directory object with the given path.
    */
   FatDir(const char *path);
   /**
    * @brief Checks if the directory object is valid.
    *
    * This method determines whether the directory object was created
    * successfully by checking if the internal FRESULT status is FR_OK.
    *
    * @return True if the directory object is valid, false otherwise.
    */
   inline bool exists() const {
      return _res == FR_OK;
   }
   /**
    * @brief Reads the next directory entry.
    *
    * This method populates the provided FILINFO structure with information about the next directory entry.
    *
    * @param info Reference to a FILINFO structure to be filled with directory entry data.
    * @return A FRESULT enumeration value indicating the result of the operation. Possible values include:
    * - FR_OK: Successful read of the directory entry.
    * - FR_DENIED: Access to the directory or entry is denied.
    * Additional error codes may be returned based on the underlying filesystem implementation.
    */
   FRESULT read(FILINFO &info);
   /**
    * @brief Seeks to the specified offset within the directory.
    *
    * This function attempts to navigate to the directory entry at the specified offset.
    * If the offset is less than the current directory pointer, it rewinds the directory to the beginning.
    *
    * @param offset The offset to seek to within the directory.
    *
    * @return FRESULT Returns FR_OK on success, or an appropriate error code on failure.
    */
   FRESULT seek(size_t offset);
   /**
    * @brief Retrieves the current position within the directory.
    *
    * This method returns the current read/write offset within the directory's data structure.
    *
    * @return The current offset within the directory.
    */
   size_t tell();
};

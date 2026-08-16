#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Block size in bytes (typically 512 for SD cards)
 */
constexpr size_t DEFAULT_BLOCK_SIZE = 512;

/**
 * @brief Erase block size in bytes (typically 4KB or 64KB for SD cards)
 */
constexpr size_t DEFAULT_ERASE_SIZE = 4096;

/**
 * @brief Sector count type for storage devices
 * This type is used for sector counts and addresses, providing consistency
 * across different storage implementations.
 */
using bd_size_t = uint32_t;

/**
 * @class StorageDevice
 * @brief Abstract interface for block storage devices (SD cards, flash, etc.)
 *
 * This interface provides a generic abstraction for block-oriented storage devices,
 * allowing the fatfs integration to work with any storage implementation.
 */
class StorageDevice {
   public:
   /**
    * @brief Initialize the storage device
    * @return Status code (0 = success)
    */
   virtual uint32_t initialize() = 0;

   /**
    * @brief Get the current status of the device
    * @return Status flags
    */
   virtual uint32_t getStatus() = 0;

   /**
    * @brief Read data from the device
    * @param buffer Buffer to receive the data
    * @param sector Starting sector number
    * @param count Number of sectors to read
    * @return Number of sectors actually read (0 on error)
    */
   virtual bd_size_t read(uint8_t *buffer, bd_size_t sector, bd_size_t count) = 0;

   /**
    * @brief Write data to the device
    * @param buffer Buffer containing data to write
    * @param sector Starting sector number
    * @param count Number of sectors to write
    * @return Number of sectors actually written (0 on error)
    */
   virtual bd_size_t write(const uint8_t *buffer, bd_size_t sector, bd_size_t count) = 0;

   /**
    * @brief Get the total number of sectors on the device
    * @return Total sector count, or 0 if not available
    */
   virtual bd_size_t getSectorCount() = 0;

   /**
    * @brief Get the erase block size in bytes
    * @return Erase block size in bytes
    */
   virtual bd_size_t getEraseSize() = 0;

   /**
    * @brief Erase a range of sectors
    * @param start Starting sector number
    * @param count Number of sectors to erase
    * @return Number of sectors erased (0 on error)
    */
   virtual bd_size_t erase(bd_size_t start, bd_size_t count) = 0;

   /**
    * @brief Get the block size in bytes
    * @return Block size in bytes
    */
   virtual bd_size_t getBlockSize() const = 0;

   virtual ~StorageDevice() = default;
};
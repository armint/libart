#pragma once

#include "Digest.h"
#include <etl/array.h>

/**
 * @brief Memory-efficient SHA-256 implementation of the Digest interface.
 *
 * This implementation is optimized for microcontrollers with limited RAM and Flash.
 * It uses a 16-word circular buffer for the message schedule instead of the standard
 * 64-word array, significantly reducing the RAM footprint.
 */
class SHA256Digest : public Digest {
   public:
   SHA256Digest();
   using Digest::update;

   /**
    * @brief Updates the digest with a chunk of data.
    * @param data Span of bytes to process.
    */
   void update(art::span<const uint8_t> data) override;

   /**
    * @brief Finalizes the hash and retrieves the 32-byte digest.
    * @param target Buffer to store the resulting hash (must be at least 32 bytes).
    * @return Size of the digest (32) or error if buffer is too small.
    */
   art::expected<size_t, Error> getDigest(art::span<uint8_t> target) override;

   /**
    * @brief Resets the digest state for a new computation.
    */
   void reset() override;

   private:
   /**
    * @brief Processes a 64-byte block using the SHA-256 algorithm.
    * @param data A 64-byte chunk of data.
    */
   void processBlock(art::span<const uint8_t, 64> data);

   /**
    * @brief Applies padding and length according to FIPS 180-4.
    */
   void finalize();

   etl::array<uint32_t, 8> intermediateHash_; // Current hash state (H0...H7)
   etl::array<uint8_t, 64> dataBlock_;        // Buffer for current block being processed
   uint64_t totalBitLength_;                  // Total number of bits processed so far
   uint32_t bufferIndex_;                     // Number of bytes currently in dataBlock_
   bool isFinalized_;                         // Flag to prevent updates after finalization
};

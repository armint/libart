#pragma once

#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include <cstdint>

class Digest {
   public:
   using Error = uint32_t;
   static constexpr Error ArrayTooSmall = 100;

   void update(const uint8_t data) {
      update(art::span(&data, 1));
   }
   /**
    * Updates the digest computation with a sequence of bytes.
    *
    * This pure virtual function must be implemented by derived classes
    * to handle the specific logic of updating the digest based on the
    * provided data.
    *
    * @param data A span containing the bytes to be incorporated into the digest computation.
    */
   virtual void update(art::span<const uint8_t> data) = 0;
   /**
    * Retrieves the current digest value into the specified target buffer.
    *
    * This pure virtual function must be implemented by derived classes
    * to compute and return the digest value. If the provided target
    * buffer is too small to hold the complete digest, an appropriate
    * error code is returned.
    *
    * @param target A span representing the buffer where the digest value
    *               will be written. Its size must be sufficient to hold the digest.
    * @return An art::expected containing either the size in bytes of the
    *         written digest or an error code indicating a failure (e.g., if
    *         the buffer is too small).
    */
   virtual art::expected<size_t, Error> getDigest(art::span<uint8_t> target) = 0;
   /**
    * Resets the internal state of the digest computation to its initial state.
    *
    * This pure virtual function must be implemented by derived classes
    * to clear any accumulated state and prepare for computing a new digest.
    */
   virtual void reset() = 0;
   virtual ~Digest() = default;
};

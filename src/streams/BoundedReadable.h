
#pragma once

#include "Readable.h"

/**
 * Represents a readable stream with bounded access, extending the functionalities of a readable interface.
 */
class BoundedReadable : public Readable {
   public:
   BoundedReadable() = default;
   BoundedReadable(BoundedReadable &&) = default;
   BoundedReadable &operator=(BoundedReadable &&) = default;
   BoundedReadable(const BoundedReadable &) = default;
   BoundedReadable &operator=(const BoundedReadable &) = default;

   using Readable::read;
   /**
    * Get the amount of bytes left in this readable
    *
    * @return the amount of bytes left in this readable
    */
   virtual size_t bytesLeft() = 0;
   /**
    * Get the current position of this stream
    *
    * @return the current position of this stream
    */
   virtual size_t position() = 0;
};

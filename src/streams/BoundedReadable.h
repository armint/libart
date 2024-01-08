
#pragma once

#include "Readable.h"

class BoundedReadable : public Readable {
   public:
   /**
    * Get the amount of bytes left in this readable
    *
    * @return the amount of bytes left in this readable
    */
   virtual size_t bytesLeft() = 0;
   /**
    * Reset the stream to the provided position
    *
    * @param position the position to reset to (default to the beginning of the stream)
    */
   virtual void reset(size_t position = 0) = 0;
   /**
    * Get the current position of this stream
    *
    * @return the current position of this stream
    */
   virtual size_t position() = 0;
};

#pragma once
#include "BoundedReadable.h"


class RandomAccessReadable : public BoundedReadable {
   public:
   /**
 * Reset the stream to the provided position
 *
 * @param position the position to reset to (default to the beginning of the stream)
 */
   virtual void reset(size_t position = 0) = 0;

};
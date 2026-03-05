#pragma once
#include "BoundedReadable.h"


class RandomAccessReadable : public BoundedReadable {
   public:
   RandomAccessReadable() = default;
   RandomAccessReadable(RandomAccessReadable&&) = default;
   RandomAccessReadable& operator=(RandomAccessReadable&&) = default;
   RandomAccessReadable(const RandomAccessReadable&) = default;
   RandomAccessReadable& operator=(const RandomAccessReadable&) = default;

   /**
    * Reset the stream to the provided position
    *
    * @param position the position to reset to (default to the beginning of the stream)
    */
   virtual void reset(size_t position = 0) = 0;
};
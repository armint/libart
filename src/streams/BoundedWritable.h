
#pragma once

#include "CountingWritable.h"

/**
 * Represents a writable stream with a bounded capacity.
 * BoundedWritable adds the concept of a maximum capacity
 * to the Writable interface and provides methods to manage
 * and query the writable state.
 */
class BoundedWritable : public CountingWritable {
   size_t capacity;

   public:
   BoundedWritable(size_t capacity)
      : capacity(capacity) {}
   /**
    * Get the total amount of bytes that can be written to this writable
    *
    * @return the total amount of bytes that can be written to this writable
    */
   size_t getCapacity() const {
      return capacity;
   }
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

   inline virtual size_t written() override {
      return position();
   }
};

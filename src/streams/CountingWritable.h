
#pragma once
#include "Writable.h"


/**
 * A writable that counts the characters written
 */
class CountingWritable : public Writable {
   public:
   /**
    * Get the total amount of bytes written to this stream
    *
    * @return the amount of bytes written
    */
   virtual size_t written() = 0;
};

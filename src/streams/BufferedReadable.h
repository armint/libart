#pragma once

#include "Readable.h"

class BufferedReadable : public Readable {
   public:
   BufferedReadable() = default;
   BufferedReadable(BufferedReadable &&) = default;
   BufferedReadable &operator=(BufferedReadable &&) = default;
   BufferedReadable(const BufferedReadable &) = default;
   BufferedReadable &operator=(const BufferedReadable &) = default;

   /**
    * Empty the buffer
    */
   virtual void clear() = 0;
};
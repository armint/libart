
#pragma once

#include <streams/Readable.h>

/**
 * An adapter to let the ArduinoJson library read from a @see Readable object
 */
class JSONReader {
   Readable &_source;

   public:
   /**
    * Wrap the readable in a JSONReader for ArduinoJson
    * @param readable the readable to wrap
    */
   JSONReader(Readable &readable)
      : _source(readable) {
   }
   // Reads one byte, or returns -1
   int read();
   // Reads several bytes, returns the number of bytes read.
   size_t readBytes(char *buffer, size_t length);
};

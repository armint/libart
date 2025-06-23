
#pragma once

#include <ArduinoJson.h>
#include <streams/Writable.h>

/**
 * An adapter to use let the ArduinoJson library write to a @see Writable object
 */
class JSONWriter {
   Writable &_writable;

   public:
   /**
    * Wrap the @see Writable in a JSONWriter for ArduinoJson
    * @param writable the writable to wrap
    */
   JSONWriter(Writable &writable)
      : _writable(writable) {
   }

   size_t write(uint8_t c) {
      return write(&c, 1);
   }
   size_t write(const uint8_t *buffer, size_t length);
};


#pragma once

#include <ArduinoJson.h>
#include <streams/Writable.h>

/**
 * An adapter to use any Writable with the ArduinoJson library
 */
class JSONWriter {
   Writable &_writable;

   public:
   /**
    * Wrap the Writable in a JSONReader for ArduinoJson
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

#include "hal.h"

size_t halChecksum(span_compat::span<const uint8_t> data) {
   size_t sum = 0;
   for(const uint8_t byte : data) {
      sum += byte;
   }
   return sum;
}

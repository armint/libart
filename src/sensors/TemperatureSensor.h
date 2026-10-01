#pragma once

#include "art_types.h"
#include "utils/compat/span.h"
#include <cstdint>

/**
 * An interface class that temperature sensors should implement
 */
class TemperatureSensor {
   public:
   TemperatureSensor() = default;

   virtual ~TemperatureSensor() = default;

   /**
    * Get the temperature of this sensor
    *
    * @return the temperature of this sensor, in degrees Celsius
    */
   virtual Temperature_t getTemperature() = 0;

   /**
    * Get the maximum temperature this sensor can measure
    *
    * @return the maximum temperature of this sensor, in degrees Celsius
    */
   virtual Temperature_t getMaxTemperature() = 0;
};

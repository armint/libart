#pragma once

#include "TemperatureSensor.h"
#include <cstddef>
#include <cstdint>

/**
 * An abstract base class for lookup table based thermocouple sensors
 */
class ThermocoupleSensor : public TemperatureSensor {
   protected:
   const Temperature_t startTemperature;
   const Temperature_t temperatureStep;

   public:
   ThermocoupleSensor(Temperature_t startTemperature, Temperature_t temperatureStep)
      : startTemperature(startTemperature)
      , temperatureStep(temperatureStep) {
   }

   ~ThermocoupleSensor() override = default;

   /**
    * Get the thermocouple voltage value at the given index in the lookup table
    */
   virtual int32_t getTableValue(size_t index) const = 0;

   /**
    * Get the number of elements in the lookup table
    */
   virtual size_t getTableSize() const = 0;

   /**
    * Get the thermocouple voltage in microvolts for a given temperature by interpolating in the lookup table
    */
   int32_t getVoltageInuVForTemperature(Temperature_t temperature);
   /**
    * Get the temperature for a given thermocouple voltage in microvolts by interpolating in the lookup table
    */
   Temperature_t getTemperatureFromVoltage(int32_t voltageInuV) const;

   /**
    * Get the maximum temperature this sensor can measure
    */
   Temperature_t getMaxTemperature() override;
};

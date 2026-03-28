#pragma once
#include "ThermocoupleSensor.h"

/**
 * A thermocouple sensor only measures a temperature difference between the hot and the cold side of the thermocouple.
 * Since the cold side of the thermocouple is seldom 0 degrees Celsius, this class can use another sensor that is placed
 * near the cold side of the thermocouple to get an absolute temperature.
 */
class CompensatedThermocoupleSensor : public TemperatureSensor {
   TemperatureSensor &coldJunctionSensor;
   ThermocoupleSensor &thermocoupleSensor;

   public:
   CompensatedThermocoupleSensor(TemperatureSensor &coldJunctionSensor, ThermocoupleSensor &thermocoupleSensor);

   Temperature_t getTemperature() override;

   virtual Temperature_t getMaxTemperature() override;
};

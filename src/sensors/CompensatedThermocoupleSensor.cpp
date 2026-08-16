#include "CompensatedThermocoupleSensor.h"

CompensatedThermocoupleSensor::CompensatedThermocoupleSensor(TemperatureSensor &coldJunctionSensor, ThermocoupleSensor &thermocoupleSensor)
   : coldJunctionSensor(coldJunctionSensor)
   , thermocoupleSensor(thermocoupleSensor) {}

Temperature_t CompensatedThermocoupleSensor::getTemperature() {
   auto coldJunctionTemperature = coldJunctionSensor.getTemperature();
   auto measuredTemperature = thermocoupleSensor.getTemperature();

   if(coldJunctionTemperature == NoTemperature || measuredTemperature == NoTemperature) {
      return NoTemperature;
   }

   // The thermocouple sensor measures the voltage between hot and cold side.
   // getTemperature() on the thermocouple sensor returns the temperature corresponding to that voltage,
   // assuming the cold side is at 0 degrees.
   int32_t measuredVoltage = thermocoupleSensor.getVoltageInuVForTemperature(measuredTemperature);

   // We need to add the voltage that a thermocouple would produce at the cold junction temperature (relative to 0C)
   int32_t coldJunctionVoltage = thermocoupleSensor.getVoltageInuVForTemperature(coldJunctionTemperature);

   // Total voltage (relative to 0C) is the sum of both
   int32_t totalVoltage = measuredVoltage + coldJunctionVoltage;

   // Convert total voltage back to temperature
   return thermocoupleSensor.getTemperatureFromVoltage(totalVoltage);
}
Temperature_t CompensatedThermocoupleSensor::getMaxTemperature() {
   return thermocoupleSensor.getMaxTemperature();
}

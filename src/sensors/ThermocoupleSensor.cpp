
#include "ThermocoupleSensor.h"

int32_t ThermocoupleSensor::getVoltageInuVForTemperature(Temperature_t temperature) {
   if(getTableSize() == 0) {
      return 0;
   }
   if(temperature <= startTemperature) {
      return getTableValue(0);
   }
   if(temperature >= getMaxTemperature()) {
      return getTableValue(getTableSize() - 1);
   }

   Temperature_t floatIndex = (temperature - startTemperature) / temperatureStep;
   auto index = static_cast<size_t>(floatIndex);
   Temperature_t remainder = floatIndex - static_cast<Temperature_t>(index);

   if(index >= getTableSize() - 1) {
      return getTableValue(getTableSize() - 1);
   }

   return static_cast<int32_t>(static_cast<Temperature_t>(getTableValue(index)) +
                               remainder * static_cast<Temperature_t>(getTableValue(index + 1) - getTableValue(index)));
}

Temperature_t ThermocoupleSensor::getTemperatureFromVoltage(int32_t voltageInuV) const {
   if(getTableSize() == 0) {
      return NoTemperature;
   }
   if(voltageInuV < getTableValue(0) || voltageInuV > getTableValue(getTableSize() - 1)) {
      return NoTemperature;
   }
   if(voltageInuV == getTableValue(0)) {
      return startTemperature;
   }

   for(size_t i = 1; i < getTableSize(); i++) {
      if(voltageInuV < getTableValue(i)) {
         return startTemperature + (static_cast<Temperature_t>(i) * temperatureStep) -
                ((static_cast<Temperature_t>(getTableValue(i)) - static_cast<Temperature_t>(voltageInuV)) * temperatureStep /
                 (static_cast<Temperature_t>(getTableValue(i)) - static_cast<Temperature_t>(getTableValue(i - 1))));
      }
   }
   return startTemperature + static_cast<Temperature_t>(getTableSize() - 1) * temperatureStep;
}

Temperature_t ThermocoupleSensor::getMaxTemperature() {
   if(getTableSize() == 0) {
      return startTemperature;
   }
   return startTemperature + static_cast<Temperature_t>(getTableSize() - 1) * temperatureStep;
}

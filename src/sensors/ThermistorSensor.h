#pragma once

#include "TemperatureSensor.h"
#include "utils/compat/span.h"
#include <array>
#include <cstdint>
#include <utils/LookupTable.h>

namespace ntc {

constexpr double constexprExp(double x) {
   double result = 1.0;
   double term = 1.0;
   for(int i = 1; i < 40; ++i) {
      term *= x / i;
      result += term;
   }
   return result;
}

} // namespace ntc

/**
 * @class ThermistorSensor
 * @brief A temperature sensor using an NTC thermistor with a pull-up resistor.
 *
 * Generates the ADC lookup table at compile time from the NTC B-parameter model:
 *   R(T) = R25 * exp(Beta * (1/(T+273.15) - 1/298.15))
 *   ADC  = R(T) * AdcMax / (R(T) + Rpullup)
 *
 * The ADC reference voltage does not affect the table because the voltage divider
 * ratio R/(R+Rpullup) is independent of the supply voltage (assuming the pull-up
 * is connected to the ADC reference supply).
 *
 * @tparam Rpullup Pull-up resistance in ohms
 * @tparam R25 NTC resistance at 25C in ohms
 * @tparam Beta NTC B-parameter (B25/100 typically)
 * @tparam StartTempTenths Start temperature in tenths of degrees (e.g. 3000 = 300.0C)
 * @tparam EndTempTenths End temperature in tenths of degrees (e.g. -300 = -30.0C)
 * @tparam StepTenths Temperature step in tenths of degrees (e.g. -50 = -5.0C)
 * @tparam AdcBits Number of ADC bits (determines AdcMax = 2^AdcBits - 1)
 */
template <uint32_t Rpullup, uint32_t R25, uint32_t Beta, int StartTempTenths, int EndTempTenths, int StepTenths,
          uint32_t AdcBits = 16>
class ThermistorSensor : public TemperatureSensor {
   static_assert(StepTenths != 0, "Step must be non-zero");
   static_assert((EndTempTenths - StartTempTenths) / StepTenths >= 0, "Temperature range must be consistent with step direction");

   static constexpr uint32_t adcMax = (1u << AdcBits) - 1;
   static constexpr size_t tableSize = static_cast<size_t>((EndTempTenths - StartTempTenths) / StepTenths) + 1;
   static constexpr Temperature_t startTemperature = static_cast<Temperature_t>(StartTempTenths) / 10.0f;
   static constexpr Temperature_t temperatureStep = static_cast<Temperature_t>(StepTenths) / 10.0f;

   static constexpr auto generateAdcTable() {
      std::array<uint16_t, tableSize> table{};
      for(size_t i = 0; i < tableSize; ++i) {
         double tempC = static_cast<double>(StartTempTenths + static_cast<int>(i) * StepTenths) / 10.0;
         double tempK = tempC + 273.15;
         double r = static_cast<double>(R25) * ntc::constexprExp(static_cast<double>(Beta) * (1.0 / tempK - 1.0 / 298.15));
         table[i] = static_cast<uint16_t>(r * static_cast<double>(adcMax) / (r + static_cast<double>(Rpullup)));
      }
      return table;
   }

   static constexpr auto adcTable = generateAdcTable();

   volatile uint16_t &adcValue;

   public:
   ThermistorSensor(volatile uint16_t &adcValue)
      : adcValue(adcValue) {}

   Temperature_t getTemperature() override {
      return getInterpolatedValueFromLookupTable(adcValue, art::span<const uint16_t>(adcTable.data(), adcTable.size()),
                                                 startTemperature, temperatureStep);
   }

   Temperature_t getMaxTemperature() override {
      return startTemperature;
   }
};

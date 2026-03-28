#pragma once

#include "TemperatureSensor.h"
#include <array>
#include <cstdint>
#include <span>
#include <utils/LookupTable.h>

namespace detail {

constexpr double constexprExp(double x) {
   double result = 1.0;
   double term = 1.0;
   for(int i = 1; i < 40; ++i) {
      term *= x / i;
      result += term;
   }
   return result;
}

/**
 * Generate an ADC lookup table for an NTC thermistor with a pull-up resistor.
 * Uses the simplified B-parameter model: R(T) = R25 * exp(Beta * (1/(T+273.15) - 1/298.15))
 * ADC value = R(T) * AdcMax / (R(T) + Rpullup)
 */
template <uint32_t Rpullup, uint32_t R25, uint32_t Beta, size_t N, int StartTempTenths, int StepTenths, uint32_t AdcMax = 65535>
constexpr std::array<uint16_t, N> generateNtcAdcTable() {
   std::array<uint16_t, N> table{};
   for(size_t i = 0; i < N; ++i) {
      double tempC = static_cast<double>(StartTempTenths + static_cast<int>(i) * StepTenths) / 10.0;
      double tempK = tempC + 273.15;
      double r = static_cast<double>(R25) * constexprExp(static_cast<double>(Beta) * (1.0 / tempK - 1.0 / 298.15));
      table[i] = static_cast<uint16_t>(r * static_cast<double>(AdcMax) / (r + static_cast<double>(Rpullup)));
   }
   return table;
}

} // namespace detail

/**
 * @class ThermistorSensor
 * @brief A temperature sensor using an NTC thermistor with a pull-up resistor.
 *
 * The lookup table maps ADC values to temperatures using linear interpolation.
 * The table can be provided directly or generated at compile time from NTC parameters.
 *
 * @tparam AdcTable A reference to a constexpr array of uint16_t ADC values, ordered by temperature
 * @tparam StartTempTenths Start temperature in tenths of degrees Celsius (e.g. 3000 = 300.0C)
 * @tparam StepTenths Temperature step in tenths of degrees Celsius (e.g. -50 = -5.0C)
 */
template <const auto &AdcTable, int StartTempTenths, int StepTenths>
class ThermistorSensor : public TemperatureSensor {
   static constexpr Temperature_t startTemperature = static_cast<Temperature_t>(StartTempTenths) / 10.0f;
   static constexpr Temperature_t temperatureStep = static_cast<Temperature_t>(StepTenths) / 10.0f;

   volatile uint16_t &adcValue;

   public:
   ThermistorSensor(volatile uint16_t &adcValue)
      : adcValue(adcValue) {}

   Temperature_t getTemperature() override {
      return getInterpolatedValueFromLookupTable(adcValue, std::span<const uint16_t>(AdcTable.data(), AdcTable.size()),
                                                 startTemperature, temperatureStep);
   }

   Temperature_t getMaxTemperature() override {
      return startTemperature;
   }
};

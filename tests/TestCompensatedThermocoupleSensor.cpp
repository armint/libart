#include <doctest/doctest.h>
#include <sensors/CompensatedThermocoupleSensor.h>
#include <sensors/KTypeThermocoupleSensor.h>

namespace {

class MockTemperatureSensor : public TemperatureSensor {
   public:
   Temperature_t temperature = 25.0f;
   Temperature_t getTemperature() override {
      return temperature;
   }
   Temperature_t getMaxTemperature() override {
      return 100.0f;
   }
};

/**
 * A concrete KType that returns a fixed temperature for testing.
 * In the libArT version, KTypeThermocoupleSensor has no ADC and getTemperature() is pure virtual.
 */
class MockKTypeThermocoupleSensor : public KTypeThermocoupleSensor {
   public:
   Temperature_t temperature = 0.0f;
   Temperature_t getTemperature() override {
      return temperature;
   }
};

} // namespace

TEST_CASE("[CompensatedThermocoupleSensor] Test getTemperature") {
   MockKTypeThermocoupleSensor tc;
   MockTemperatureSensor cj;
   CompensatedThermocoupleSensor sensor(cj, tc);

   SUBCASE("0C cold junction, 0C measured") {
      cj.temperature = 0.0f;
      tc.temperature = 0.0f;
      CHECK(sensor.getTemperature() == doctest::Approx(0.0f).epsilon(0.1));
   }

   SUBCASE("25C cold junction, 0C measured difference") {
      cj.temperature = 25.0f;
      tc.temperature = 0.0f;
      // total voltage = V(0C) + V(25C) = 0 + 1000 = 1000uV -> 25C
      CHECK(sensor.getTemperature() == doctest::Approx(25.0f).epsilon(0.1));
   }

   SUBCASE("25C cold junction, 5C measured difference") {
      cj.temperature = 25.0f;
      tc.temperature = 5.0f;
      // V(5C) = 198uV, V(25C) = 1000uV, total = 1198uV
      // Between V(25C)=1000 and V(30C)=1203:
      // T = 25 + (1198 - 1000) * 5 / (1203 - 1000) = 25 + 198*5/203 = 25 + 4.877 = 29.877
      CHECK(sensor.getTemperature() == doctest::Approx(29.877f).epsilon(0.1));
   }

   SUBCASE("NoTemperature handling - cold junction") {
      cj.temperature = NoTemperature;
      tc.temperature = 0.0f;
      CHECK(sensor.getTemperature() == NoTemperature);
   }

   SUBCASE("NoTemperature handling - thermocouple") {
      cj.temperature = 25.0f;
      tc.temperature = NoTemperature;
      CHECK(sensor.getTemperature() == NoTemperature);
   }
}

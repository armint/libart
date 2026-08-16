#include <doctest/doctest.h>
#include <sensors/KTypeThermocoupleSensor.h>

namespace {
class TestableKType : public KTypeThermocoupleSensor {
   public:
   Temperature_t getTemperature() override { return 0; }
};
} // namespace

TEST_CASE("[KTypeThermocoupleSensor] Test getVoltageInuVForTemperature") {
   TestableKType sensor;

   SUBCASE("Boundary conditions") {
      CHECK(sensor.getVoltageInuVForTemperature(-10.0f) == 0);
      CHECK(sensor.getVoltageInuVForTemperature(0.0f) == 0);

      Temperature_t maxTemp = sensor.getMaxTemperature();
      // voltageTable[291] = 54819
      CHECK(sensor.getVoltageInuVForTemperature(maxTemp) == 54819);
      CHECK(sensor.getVoltageInuVForTemperature(maxTemp + 10.0f) == 54819);
   }

   SUBCASE("Exact table values") {
      // 5C step. Index 1 is 5C.
      CHECK(sensor.getVoltageInuVForTemperature(5.0f) == 198);
      // Index 2 is 10C.
      CHECK(sensor.getVoltageInuVForTemperature(10.0f) == 397);
   }

   SUBCASE("Interpolated values") {
      // 2.5C is halfway between 0C (0uV) and 5C (198uV)
      // (0 + 198) / 2 = 99
      CHECK(sensor.getVoltageInuVForTemperature(2.5f) == 99);

      // 7.5C is halfway between 5C (198uV) and 10C (397uV)
      // 198 + 0.5 * (397 - 198) = 198 + 0.5 * 199 = 198 + 99.5 = 297.5 -> 297 (integer truncation)
      CHECK(sensor.getVoltageInuVForTemperature(7.5f) == 297);
   }
}

TEST_CASE("[KTypeThermocoupleSensor] Test getTemperatureFromVoltage") {
   TestableKType sensor;

   SUBCASE("Exact table values") {
      CHECK(sensor.getTemperatureFromVoltage(0) == 0.0f);
      CHECK(sensor.getTemperatureFromVoltage(198) == doctest::Approx(5.0f).epsilon(0.01));
   }

   SUBCASE("Out of range") {
      CHECK(sensor.getTemperatureFromVoltage(-1) == NoTemperature);
      CHECK(sensor.getTemperatureFromVoltage(60000) == NoTemperature);
   }

   SUBCASE("Max temperature") {
      CHECK(sensor.getMaxTemperature() == doctest::Approx(1455.0f).epsilon(0.1));
   }
}

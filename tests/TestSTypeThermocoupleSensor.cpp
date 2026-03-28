#include <doctest/doctest.h>
#include <sensors/STypeThermocoupleSensor.h>

namespace {
class TestableSType : public STypeThermocoupleSensor {
   public:
   Temperature_t getTemperature() override { return 0; }
};
} // namespace

TEST_CASE("[STypeThermocoupleSensor] Test getVoltageInuVForTemperature") {
   TestableSType sensor;

   SUBCASE("Boundary conditions") {
      CHECK(sensor.getVoltageInuVForTemperature(-60.0f) == -236);
      CHECK(sensor.getVoltageInuVForTemperature(-50.0f) == -236);

      Temperature_t maxTemp = sensor.getMaxTemperature();
      // sTypeVoltageTable[363] = 18661
      CHECK(sensor.getVoltageInuVForTemperature(maxTemp) == 18661);
      CHECK(sensor.getVoltageInuVForTemperature(maxTemp + 10.0f) == 18661);
   }

   SUBCASE("Exact table values") {
      // 0C. Index 10
      CHECK(sensor.getVoltageInuVForTemperature(0.0f) == 0);
      // -45C. Index 1
      CHECK(sensor.getVoltageInuVForTemperature(-45.0f) == -215);
      // 5C. Index 11
      CHECK(sensor.getVoltageInuVForTemperature(5.0f) == 27);
   }

   SUBCASE("Interpolated values") {
      // -2.5C is halfway between -5C (-27uV) and 0C (0uV)
      // -27 + 0.5 * (0 - (-27)) = -27 + 13.5 = -13.5 -> -13
      CHECK(sensor.getVoltageInuVForTemperature(-2.5f) == -13);

      // 2.5C is halfway between 0C (0uV) and 5C (27uV)
      // 0 + 0.5 * 27 = 13.5 -> 13
      CHECK(sensor.getVoltageInuVForTemperature(2.5f) == 13);
   }
}

TEST_CASE("[STypeThermocoupleSensor] Test getTemperatureFromVoltage") {
   TestableSType sensor;

   SUBCASE("Exact table values") {
      CHECK(sensor.getTemperatureFromVoltage(0) == 0.0f);
      CHECK(sensor.getTemperatureFromVoltage(27) == doctest::Approx(5.0f).epsilon(0.01));
   }

   SUBCASE("Out of range") {
      CHECK(sensor.getTemperatureFromVoltage(-300) == NoTemperature);
      CHECK(sensor.getTemperatureFromVoltage(20000) == NoTemperature);
   }
}

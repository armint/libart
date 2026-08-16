#pragma once

#include "ThermocoupleSensor.h"
#include <cstdint>
#include <iterator>

/**
 * A class providing K-type thermocouple voltage lookup table and interpolation.
 * Subclass and implement getTemperature() to add hardware-specific ADC reading.
 */
class KTypeThermocoupleSensor : public ThermocoupleSensor {
   public:
   static constexpr uint32_t tcaReference = 350;
   static constexpr Temperature_t kTypeStartTemperature = 0;
   static constexpr Temperature_t kTypeTemperatureStep = 5;
   // Amplification factor numerator and denominator: (1 + 100k/1.96k) = 2549/49
   static constexpr int64_t amplificationNumerator = 49000;
   static constexpr int64_t amplificationDenominator = 2549;

   // The thermocouple voltage in microvolts, from 0C to 1365C in steps of 5C
   static constexpr uint16_t kTypeVoltageTable[] = {
      0,     198,   397,   597,   798,   1000,  1203,  1407,  1612,  1817,  2023,  2230,  2436,  2644,  2851,  3059,
      3267,  3474,  3682,  3889,  4096,  4303,  4509,  4715,  4920,  5124,  5328,  5532,  5735,  5937,  6138,  6339,
      6540,  6741,  6941,  7140,  7340,  7540,  7739,  7939,  8138,  8338,  8539,  8739,  8940,  9141,  9343,  9545,
      9747,  9950,  10153, 10357, 10561, 10766, 10971, 11176, 11382, 11588, 11795, 12001, 12209, 12416, 12624, 12831,
      13040, 13248, 13457, 13665, 13874, 14084, 14293, 14503, 14713, 14923, 15133, 15343, 15554, 15764, 15975, 16186,
      16397, 16608, 16820, 17031, 17243, 17455, 17667, 17879, 18091, 18303, 18516, 18728, 18941, 19154, 19366, 19579,
      19792, 20005, 20218, 20431, 20644, 20857, 21071, 21284, 21497, 21710, 21924, 22137, 22350, 22563, 22776, 22990,
      23203, 23416, 23629, 23842, 24055, 24267, 24480, 24693, 24905, 25118, 25330, 25543, 25755, 25967, 26179, 26390,
      26602, 26814, 27025, 27236, 27447, 27658, 27869, 28079, 28289, 28500, 28710, 28919, 29129, 29338, 29548, 29757,
      29965, 30174, 30382, 30590, 30798, 31006, 31213, 31421, 31628, 31834, 32041, 32247, 32453, 32659, 32865, 33070,
      33275, 33480, 33685, 33889, 34093, 34297, 34501, 34704, 34908, 35110, 35313, 35516, 35718, 35920, 36121, 36323,
      36524, 36725, 36925, 37126, 37326, 37526, 37725, 37925, 38124, 38323, 38522, 38720, 38918, 39116, 39314, 39511,
      39708, 39905, 40101, 40298, 40494, 40690, 40885, 41081, 41276, 41470, 41665, 41859, 42053, 42247, 42440, 42633,
      42826, 43019, 43211, 43403, 43595, 43787, 43978, 44169, 44359, 44550, 44740, 44929, 45119, 45308, 45497, 45685,
      45873, 46061, 46249, 46436, 46623, 46809, 46995, 47181, 47367, 47552, 47737, 47921, 48105, 48289, 48473, 48656,
      48838, 49021, 49202, 49384, 49565, 49746, 49926, 50106, 50286, 50465, 50644, 50822, 51000, 51178, 51355, 51532,
      51708, 51885, 52060, 52235, 52410, 52585, 52759, 52932, 53106, 53279, 53451, 53623, 53795, 53967, 54138, 54308,
      54479, 54649, 54819,
   };

   KTypeThermocoupleSensor()
      : ThermocoupleSensor(kTypeStartTemperature, kTypeTemperatureStep) {}

   int32_t getTableValue(size_t index) const override {
      return kTypeVoltageTable[index];
   }

   size_t getTableSize() const override {
      return std::size(kTypeVoltageTable);
   }
};

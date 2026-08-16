#pragma once
#include "art_types.h"
#include <cstdint>
#include <span>

template <typename R, typename T, typename V>
R getInterpolatedValueFromLookupTable(V lookupValue, std::span<const T> lookupTable, R startValue, R stepValue) {
   if(lookupValue < static_cast<V>(lookupTable[0]) || lookupValue > static_cast<V>(lookupTable[lookupTable.size() - 1])) {
      return NoTemperature;
   }
   if(lookupValue == static_cast<V>(lookupTable[0])) {
      return startValue;
   }
   R value = startValue;
   for(size_t i = 0; i < lookupTable.size(); i++) {
      if(lookupValue < static_cast<V>(lookupTable[i])) {
         value = startValue + (static_cast<R>(i) * stepValue) -
                 ((static_cast<R>(lookupTable[i]) - static_cast<R>(lookupValue)) * stepValue /
                  (static_cast<R>(lookupTable[i]) - static_cast<R>(lookupTable[i - 1])));
         break;
      }
   }
   return value;
}


#pragma once
#include <cstring>

#include "ArrayReadable.h"

class StringReadable : public ArrayReadable {
   public:
   StringReadable(const char *str)
      : ArrayReadable(std::span<const char>(str, strlen(str))) {
   }
};

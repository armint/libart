#pragma once
#include "Level.h"
#include "streams/Writable.h"

namespace logging {
class Formatter {
   public:
   virtual ~Formatter() = default;
   virtual art::expected<size_t, Writable::Error> formatPrefix(Writable &sink, Level level, const char *topic) = 0;
   virtual art::expected<size_t, Writable::Error> formatSuffix(Writable &sink) = 0;
};
} // namespace logging

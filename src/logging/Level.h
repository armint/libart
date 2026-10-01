#pragma once

#include <cstring>
#include <optional>

namespace logging {
enum class Level { Trace, Debug, Info, Warning, Error };

inline std::optional<Level> levelFromString(const char *str) {
   if(!str)
      return std::nullopt;
   if(std::strcmp(str, "Trace") == 0)
      return Level::Trace;
   if(std::strcmp(str, "Debug") == 0)
      return Level::Debug;
   if(std::strcmp(str, "Info") == 0)
      return Level::Info;
   if(std::strcmp(str, "Warning") == 0)
      return Level::Warning;
   if(std::strcmp(str, "Error") == 0)
      return Level::Error;
   return std::nullopt;
}
} // namespace logging
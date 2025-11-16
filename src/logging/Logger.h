#pragma once
#include "Level.h"

#include <utility>
#include <cstdarg>

namespace logging {
class Logger {
   protected:
   const char *topic_;
   Level level_ = Level::Info;

   public:
   static constexpr char *levelStrings[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR" };

   explicit Logger(const char *topic)
      : topic_(topic) {
   }

   void log(Level level, const char *format, ...);
   void trace(const char *format, ...);
   void debug(const char *format, ...);
   void info(const char *format, ...);
   void warn(const char *format, ...);
   void error(const char *format, ...);

   protected:
   void vlog(Level level, const char *format, va_list args);


};
}
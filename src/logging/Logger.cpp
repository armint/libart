#include "Logger.h"

#include <cstdarg>
#include <cstdio>

namespace logging {

void Logger::vlog(Level level, const char *format, va_list args) {
   // Minimal default implementation: write to stdout with a simple prefix
   const char *levelStr = levelStrings[static_cast<int>(level)];
   if (topic_) {
      std::fprintf(stdout, "[%s][%s] ", topic_, levelStr);
   } else {
      std::fprintf(stdout, "[%s] ", levelStr);
   }
   std::vfprintf(stdout, format, args);
   std::fprintf(stdout, "\n");
   std::fflush(stdout);
}

void Logger::log(Level level, const char *format, ...) {
   va_list args;
   va_start(args, format);
   vlog(level, format, args);
   va_end(args);
}

void Logger::trace(const char *format, ...) {
   va_list args;
   va_start(args, format);
   vlog(Level::Trace, format, args);
   va_end(args);
}

void Logger::debug(const char *format, ...) {
   va_list args;
   va_start(args, format);
   vlog(Level::Debug, format, args);
   va_end(args);
}

void Logger::info(const char *format, ...) {
   va_list args;
   va_start(args, format);
   vlog(Level::Info, format, args);
   va_end(args);
}

void Logger::warn(const char *format, ...) {
   va_list args;
   va_start(args, format);
   vlog(Level::Warning, format, args);
   va_end(args);
}

void Logger::error(const char *format, ...) {
   va_list args;
   va_start(args, format);
   vlog(Level::Error, format, args);
   va_end(args);
}

}
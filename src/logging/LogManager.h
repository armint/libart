#pragma once
#include "Level.h"
#include "streams/Writable.h"

namespace logging {
class Logger;
class Formatter;
} // namespace logging

class LogManager {
   private:
   LogManager() = default;
   Writable *sink_ = nullptr;
   logging::Formatter *formatter_ = nullptr;
   logging::Level globalLevel_ = logging::Level::Trace;
   logging::Logger *firstLogger_ = nullptr;

   public:
   static LogManager &getInstance() {
      static LogManager instance_;
      return instance_;
   }

   [[nodiscard]] Writable *sink() const {
      return sink_;
   }

   void set_sink(Writable *sink) {
      sink_ = sink;
   }

   [[nodiscard]] logging::Formatter *formatter() const {
      return formatter_;
   }

   void set_formatter(logging::Formatter *formatter) {
      formatter_ = formatter;
   }

   [[nodiscard]] logging::Level global_level() const {
      return globalLevel_;
   }

   void set_global_level(logging::Level level) {
      globalLevel_ = level;
   }

   void registerLogger(logging::Logger *logger);
   logging::Logger *findLogger(const char *topic);
};

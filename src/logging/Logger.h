#pragma once
#include "Level.h"
#include "LogManager.h"

#include <utility>

namespace logging {
class Logger {
   protected:
   const char *topic_;
   Level level_ = Level::Info;

   public:
   static inline constexpr const char *levelStrings[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR" };

   explicit Logger(const char *topic)
      : topic_(topic) {
   }

   template <typename... Args>
   void log(Level level, const char *format, Args &&... args) {
      vlog(level, format, std::forward<Args>(args)...);
   }

   template <typename... Args>
   void trace(const char *format, Args &&... args) {
      vlog(Level::Trace, format, std::forward<Args>(args)...);
   }

   template <typename... Args>
   void debug(const char *format, Args &&... args) {
      vlog(Level::Debug, format, std::forward<Args>(args)...);
   }

   template <typename... Args>
   void info(const char *format, Args &&... args) {
      vlog(Level::Info, format, std::forward<Args>(args)...);
   }

   template <typename... Args>
   void warn(const char *format, Args &&... args) {
      vlog(Level::Warning, format, std::forward<Args>(args)...);
   }

   template <typename... Args>
   void error(const char *format, Args &&... args) {
      vlog(Level::Error, format, std::forward<Args>(args)...);
   }

   protected:
   template <typename... Args>
   void vlog(Level level, const char *format, Args &&... args) {
      const char *levelStr = levelStrings[static_cast<int>(level)];
      if(auto *sink = LogManager::getInstance().sink()) {
         if(topic_) {
            (void)sink->printf("[%s][%s] ", topic_, levelStr);
         } else {
            (void)sink->printf("[%s] ", levelStr);
         }
         (void)sink->printf(format, std::forward<Args>(args)...);
         (void)sink->println();
         (void)sink->flush();
      }
   }


};
}
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

   // Logging level control
   void set_level(Level level) noexcept {
      level_ = level;
   }

   [[nodiscard]] Level level() const noexcept {
      return level_;
   }

   [[nodiscard]] bool isEnabled(Level level) const noexcept {
      return static_cast<int>(level) >= static_cast<int>(level_);
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
      // Fast-path exit: no sink or level disabled
      auto *sink = LogManager::getInstance().sink();
      if(!sink || !isEnabled(level)) {
         return;
      }

      const char *levelStr = levelStrings[static_cast<int>(level)];
      if(topic_) {
         (void)sink->printf("[%s][%s] ", topic_, levelStr);
      } else {
         (void)sink->printf("[%s] ", levelStr);
      }
      (void)sink->printf(format, std::forward<Args>(args)...);
      (void)sink->println();
      (void)sink->flush();
   }


};
}

// Convenience macros for zero-cost (when disabled) logging at call sites.
// You can set a compile-time minimum level to compile out lower levels entirely.
// Example: add -DLOG_COMPILED_MIN_LEVEL=logging::Level::Info to your compiler flags
// to remove TRACE and DEBUG calls from the binary.

#ifndef LOG_COMPILED_MIN_LEVEL
#define LOG_COMPILED_MIN_LEVEL logging::Level::Trace
#endif

#define LOG__LEVEL_VALUE(L) static_cast<int>(L)
#define LOG__CT_ENABLED(L) (LOG__LEVEL_VALUE(L) >= LOG__LEVEL_VALUE(LOG_COMPILED_MIN_LEVEL))

#define LOG_TRACE(LOGGER, FMT, ...)                                                                 \
   do {                                                                                              \
      if(LOG__CT_ENABLED(logging::Level::Trace) && (LOGGER).isEnabled(logging::Level::Trace)) {      \
         (LOGGER).trace((FMT), ##__VA_ARGS__);                                                       \
      }                                                                                              \
   } while(0)

#define LOG_DEBUG(LOGGER, FMT, ...)                                                                 \
   do {                                                                                              \
      if(LOG__CT_ENABLED(logging::Level::Debug) && (LOGGER).isEnabled(logging::Level::Debug)) {      \
         (LOGGER).debug((FMT), ##__VA_ARGS__);                                                       \
      }                                                                                              \
   } while(0)

#define LOG_INFO(LOGGER, FMT, ...)                                                                  \
   do {                                                                                              \
      if(LOG__CT_ENABLED(logging::Level::Info) && (LOGGER).isEnabled(logging::Level::Info)) {        \
         (LOGGER).info((FMT), ##__VA_ARGS__);                                                        \
      }                                                                                              \
   } while(0)

#define LOG_WARN(LOGGER, FMT, ...)                                                                  \
   do {                                                                                              \
      if(LOG__CT_ENABLED(logging::Level::Warning) && (LOGGER).isEnabled(logging::Level::Warning)) {  \
         (LOGGER).warn((FMT), ##__VA_ARGS__);                                                        \
      }                                                                                              \
   } while(0)

#define LOG_ERROR(LOGGER, FMT, ...)                                                                 \
   do {                                                                                              \
      if(LOG__CT_ENABLED(logging::Level::Error) && (LOGGER).isEnabled(logging::Level::Error)) {      \
         (LOGGER).error((FMT), ##__VA_ARGS__);                                                       \
      }                                                                                              \
   } while(0)
#pragma once
#include "Formatter.h"
#include "Logger.h"

namespace logging {
class DefaultFormatter : public Formatter {
   public:
   std::expected<size_t, Writable::Error> formatPrefix(Writable &sink, Level level, const char *topic) override {
      const char *levelStr = Logger::levelStrings[static_cast<int>(level)];
      if(topic) {
         return sink.printf("[%s][%s] ", topic, levelStr);
      }
      return sink.printf("[%s] ", levelStr);
   }

   std::expected<size_t, Writable::Error> formatSuffix(Writable &sink) override {
      auto ret = sink.println();
      if(!ret) {
         return ret;
      }
      auto fl = sink.flush();
      if(!fl) {
         return std::unexpected(fl.error());
      }
      return ret;
   }
};
} // namespace logging

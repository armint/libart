#pragma once
#include "streams/Writable.h"


class LogManager {
   private:
   LogManager() = default;
   Writable *sink_ = nullptr;

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

};
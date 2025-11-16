#pragma once
#include "streams/Writable.h"


class LogManager {
   private:
   LogManager() = default;
   Writable* sink_ = nullptr;
   public:
   static LogManager& getInstance() {
      static LogManager instance_;
      return instance_;
   }

};
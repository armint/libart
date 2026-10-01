#include "LogManager.h"
#include "Logger.h"
#include <cstring>

void LogManager::registerLogger(logging::Logger *logger) {
   logger->nextLogger_ = firstLogger_;
   firstLogger_ = logger;
}

void LogManager::unregisterLogger(logging::Logger *logger) {
   for(auto **link = &firstLogger_; *link != nullptr; link = &(*link)->nextLogger_) {
      if(*link == logger) {
         *link = logger->nextLogger_;
         logger->nextLogger_ = nullptr;
         return;
      }
   }
}

logging::Logger *LogManager::findLogger(const char *topic) {
   if(!topic)
      return nullptr;
   for(auto *l = firstLogger_; l != nullptr; l = l->nextLogger_) {
      if(l->topic() && std::strcmp(l->topic(), topic) == 0) {
         return l;
      }
   }
   return nullptr;
}

#include "SettingsManager.h"
#include "streams/CountingWritableWrapper.h"
#include "utils/JSONReader.h"
#include "utils/JSONWriter.h"

SettingsManager &SettingsManager::getInstance() {
   static SettingsManager instance;
   return instance;
}

std::expected<void, Readable::Error> SettingsManager::load(Readable &readable) {
   StaticJsonDocument<3000> doc;
   JSONReader reader(readable);
   auto r = deserializeJson(doc, reader);
   if(r != DeserializationError::Ok) {
      return std::unexpected(Readable::ReadError);
   }
   auto root = doc.as<JsonObject>();
   for(auto &setting : _settings) {
      auto result = setting.load(root);
      if(!result) {
         return std::unexpected<Readable::Error>(result.error());
      }
   }
   return {};
}
std::expected<size_t, Writable::Error> SettingsManager::save(Writable &writable) {
   StaticJsonDocument<3000> doc;
   // This is silly, but there doesn't seem to be another way to create a root object
   doc[""] = nullptr;
   doc.remove("");
   doc.garbageCollect();
   auto root = doc.as<JsonObject>();
   for(auto &setting : _settings) {
      auto result = setting.save(root);
      if(!result) {
         return std::unexpected(result.error());
      }
      if(doc.overflowed()) {
         return std::unexpected(Writable::CouldNotWrite);
      }
   }
   CountingWritableWrapper cww(writable);
   JSONWriter jsonWriter(cww);
   size_t written = serializeJsonPretty(doc, jsonWriter);
   if(written != cww.written()) {
      return std::unexpected(Writable::CouldNotWrite);
   }
   return written;
}

bool SettingsManager::isModified() {
   for(auto &setting : _settings) {
      if(setting.isModified()) {
         return true;
      }
   }
   return false;
}

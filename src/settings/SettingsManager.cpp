
#include "SettingsManager.h"
#include "streams/CountingWritableWrapper.h"
#include "utils/JSONReader.h"
#include "utils/JSONWriter.h"

SettingsManager &SettingsManager::getInstance() {
   static SettingsManager instance;
   return instance;
}

art::expected<void, Readable::Error> SettingsManager::load(Readable &readable) {
   StaticJsonDocument<3000> doc;
   JSONReader reader(readable);
   auto r = deserializeJson(doc, reader);
   if(r != DeserializationError::Ok) {
      return art::unexpected(Readable::ReadError);
   }
   auto root = doc.as<JsonObject>();
   for(auto &setting : _settings) {
      auto result = setting.load(root);
      if(!result) {
         return art::unexpected<Readable::Error>(result.error());
      }
   }
   return {};
}
art::expected<size_t, Writable::Error> SettingsManager::save(Writable &writable) {
   StaticJsonDocument<3000> doc;
   auto root = doc.to<JsonObject>();
   for(auto &setting : _settings) {
      auto result = setting.save(root);
      if(!result) {
         return art::unexpected(result.error());
      }
      if(doc.overflowed()) {
         return art::unexpected(Writable::CouldNotWrite);
      }
   }
   CountingWritableWrapper cww(writable);
   JSONWriter jsonWriter(cww);
   size_t written = serializeJsonPretty(doc, jsonWriter);
   if(written != cww.written()) {
      return art::unexpected(Writable::CouldNotWrite);
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

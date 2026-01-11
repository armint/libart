
#pragma once

#include "SettingBase.h"
#include <ArduinoJson.h>
#include <streams/Readable.h>
#include <streams/Writable.h>
/**
 * A simple class to manager settings, using ArduinoJson. See @Setting<T> for more details
 */
class SettingsManager {
   protected:
   SettingBase::List _settings;

   public:
   friend class SettingBase;
   SettingsManager()
      : _settings(true) {}

   static SettingsManager &getInstance();

   /**
    * Load the settings from the provided @Readable
    * @param readable the readable to load the settings from
    * @return a @Readable::Error if the settings could not be loaded from the provided readable
    */
   std::expected<void, Readable::Error> load(Readable &readable);
   std::expected<size_t, Writable::Error> save(Writable &writable);
   bool isModified();
};

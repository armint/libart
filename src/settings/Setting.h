
#pragma once
#include "SettingBase.h"

/**
 * This class is used to manage a single setting. The Setting object can be created anywhere in the code, and will
 * automatically link to the list in the @SettingsManager.
 *
 * @tparam T The value type of this setting, for example int or const char*
 */
template <typename T>
class Setting : public SettingBase {
   protected:
   T _value;

   public:
   T getValue() const {
      return _value;
   }
   void setValue(T value) {
      if(value != _value) {
         _value = value;
         _modified = true;
      }
   }

   // Allow implicit conversion to the underlying value type
   operator T() const {
      return _value;
   }

   Setting<T> &operator=(T value) {
      setValue(value);
      return *this;
   }

   public:
   /**
    * Create a setting with a name and a default value
    *
    * @param name the name of the setting. Only the pointer is saved in the setting object, so the c-string name
    * points to should remain valid and constant for as long as the Setting object is valid!
    * @param defaultValue The default value for this setting
    */
   Setting(const char *name, T defaultValue)
      : SettingBase(name)
      , _value(defaultValue) {};

   /**
    * Create a setting with a name and a default value, in a section
    *
    * @param section the name of the section this setting belongs to. Only the pointer is saved in the setting object,
    * so the c-string section points to should remain valid and constant for as long as the Setting object is valid!
    * @param name the name of the setting. Only the pointer is saved in the Setting object, so the c-string name
    * points to should remain valid and constant for as long as the Setting object is valid!
    * @param defaultValue the default value for this setting
    */
   Setting(const char *section, const char *name, T defaultValue)
      : SettingBase(name, section)
      , _value(defaultValue) {};

   /**
    * Load the value of this setting from json, the setting is a child of the provided JsonObject
    *
    * @param parent the JsonObject this setting is a child of
    * @return An error if the setting could not be loaded
    */
   virtual std::expected<void, Readable::Error> load(JsonObject &parent) override;
   /**
    * Store this setting as a child of the provided JsonObject
    *
    * @param parent the JsonObject this setting is a child of
    * @return An error if the setting could not be saved
    */
   virtual std::expected<size_t, Writable::Error> save(JsonObject &parent) override;
};

template <typename T>
std::expected<size_t, Writable::Error> Setting<T>::save(JsonObject &parent) {
   if(getSection() != nullptr) {
      parent[getSection()][getName()] = _value;
   } else {
      parent[getName()] = _value;
   }
   _modified = false;
   return {};
}

template <typename T>
std::expected<void, Readable::Error> Setting<T>::load(JsonObject &parent) {
   _modified = false;
   if(getSection() != nullptr) {
      if(parent.containsKey(getSection()) && parent[getSection()].containsKey(getName())) {
         _value = parent[getSection()][getName()];
      } else {
         _modified = true;
      }
   } else {
      if(parent.containsKey(getName())) {
         _value = parent[getName()];
      } else {
         _modified = true;
      }
   }
   return {};
}

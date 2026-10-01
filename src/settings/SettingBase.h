
#pragma once
#include <ArduinoJson.h>
#include <streams/Readable.h>
#include <streams/Writable.h>
#include <utils/lists.h>

/**
 * Base class for a setting. Provides the basic functionality of linking the setting to the @SettingsManager and
 * contains the name and section of the setting. All settings should inherit from this class
 */
class SettingBase {
   protected:
   bool _modified = false;
   const char *_name;
   const char *_section;

   protected:
   micro_os_plus::utils::double_list_links _listLinks;

   public:
   using List = micro_os_plus::utils::intrusive_list<SettingBase, micro_os_plus::utils::double_list_links, &SettingBase::_listLinks>;

   /**
    * Create a setting with a name and a default value, in a section
    *
    * @param name the name of the setting. Only the pointer is saved in the setting object, so the c-string the name
    * points to should remain valid as long as the Setting object is valid!
    * @param section the name of the section this setting belongs to. Only the pointer is saved in the setting object,
    * so the c-string the name points to should remain valid as long as the Setting object is valid!
    */
   SettingBase(const char *name, const char *section = nullptr);

   virtual ~SettingBase() {
      // Make sure the settings list remains consistent
      _listLinks.unlink();
   }

   /**
    *
    * @return true if this setting is modified since the last call to load or save
    */
   bool isModified() const {
      return _modified;
   }

   /**
    * Get the name (key) of this setting
    *
    * @return the name of this setting
    */
   const char *getName() const {
      return _name;
   }

   /**
    * get the section of this setting
    *
    * @return the section of this setting, or nullptr if this setting doesn't belong to a sections
    */
   const char *getSection() const {
      return _section;
   }

   /**
    * Load this setting, which is a child of the provided JsonObject
    *
    * @param parent the JsonObject this setting is a child of
    * @return An error if the setting could not be loaded
    */
   virtual art::expected<void, Readable::Error> load(JsonObject &parent) = 0;
   /**
    * Store this setting as a child of the provided JsonObject
    *
    * @param parent the JsonObject this setting is a child of
    * @return An error if the setting could not be saved
    */
   virtual art::expected<size_t, Writable::Error> save(JsonObject &parent) = 0;
};

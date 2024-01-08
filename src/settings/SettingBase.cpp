
#include "SettingBase.h"
#include "SettingsManager.h"

SettingBase::SettingBase(const char *name, const char *section)
   : _name(name)
   , _section(section) {
   SettingsManager::getInstance()._settings.link(*this);
}
#include "ini_settings_interface.h"
#include "common/log.h"
#include <algorithm>
Log_SetChannel(INISettingsInterface);

INISettingsInterface::INISettingsInterface(const char* filename) : m_filename(filename), m_ini(true, true)
{
  SI_Error err = m_ini.LoadFile(filename);
  if (err != SI_OK)
    Log_WarningPrintf("Settings could not be loaded from '%s', defaults will be used.", filename);
}

INISettingsInterface::~INISettingsInterface()
{
  if (m_dirty)
  {
    SI_Error err = m_ini.SaveFile(m_filename.c_str(), false);
    if (err != SI_OK)
      Log_WarningPrintf("Failed to save settings to '%s'.", m_filename.c_str());
  }
}

void INISettingsInterface::Clear()
{
  m_ini.Reset();
}

int INISettingsInterface::GetIntValue(const char* section, const char* key, int default_value /*= 0*/)
{
  return static_cast<int>(m_ini.GetLongValue(section, key, default_value));
}

float INISettingsInterface::GetFloatValue(const char* section, const char* key, float default_value /*= 0.0f*/)
{
  return static_cast<float>(m_ini.GetDoubleValue(section, key, default_value));
}

bool INISettingsInterface::GetBoolValue(const char* section, const char* key, bool default_value /*= false*/)
{
  return m_ini.GetBoolValue(section, key, default_value);
}

std::string INISettingsInterface::GetStringValue(const char* section, const char* key,
                                                 const char* default_value /*= ""*/)
{
  return m_ini.GetValue(section, key, default_value);
}

void INISettingsInterface::SetIntValue(const char* section, const char* key, int value)
{
  m_dirty = true;
  m_ini.SetLongValue(section, key, static_cast<long>(value), nullptr, false, true);
}

void INISettingsInterface::SetFloatValue(const char* section, const char* key, float value)
{
  m_dirty = true;
  m_ini.SetDoubleValue(section, key, static_cast<double>(value), nullptr, true);
}

void INISettingsInterface::SetBoolValue(const char* section, const char* key, bool value)
{
  m_dirty = true;
  m_ini.SetBoolValue(section, key, value, nullptr, true);
}

void INISettingsInterface::SetStringValue(const char* section, const char* key, const char* value)
{
  m_dirty = true;
  m_ini.SetValue(section, key, value, nullptr, true);
}

void INISettingsInterface::DeleteValue(const char* section, const char* key)
{
  m_dirty = true;
  m_ini.Delete(section, key);
}

std::vector<std::string> INISettingsInterface::GetStringList(const char* section, const char* key)
{
  std::list<CSimpleIniA::Entry> entries;
  if (!m_ini.GetAllValues(section, key, entries))
    return {};

  std::vector<std::string> ret;
  ret.reserve(entries.size());
  std::transform(entries.begin(), entries.end(), std::back_inserter(ret),
                 [](const CSimpleIniA::Entry& it) { return std::string(it.pItem); });
  return ret;
}

void INISettingsInterface::SetStringList(const char* section, const char* key,
                                         const std::vector<std::string_view>& items)
{
  m_dirty = true;
  m_ini.Delete(section, key);

  for (const std::string_view& sv : items)
    m_ini.SetValue(section, key, std::string(sv).c_str(), nullptr, false);
}

bool INISettingsInterface::RemoveFromStringList(const char* section, const char* key, const char* item)
{
  m_dirty = true;
  return m_ini.DeleteValue(section, key, item, true);
}

bool INISettingsInterface::AddToStringList(const char* section, const char* key, const char* item)
{
  std::list<CSimpleIniA::Entry> entries;
  if (m_ini.GetAllValues(section, key, entries) &&
      std::find_if(entries.begin(), entries.end(),
                   [item](const CSimpleIniA::Entry& e) { return (std::strcmp(e.pItem, item) == 0); }) != entries.end())
  {
    return false;
  }

  m_dirty = true;
  m_ini.SetValue(section, key, item, nullptr, false);
  return true;
}

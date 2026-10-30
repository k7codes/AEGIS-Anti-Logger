#pragma once
#include <string>
#include <cstdint>
#include "common.hpp"

namespace aegis {

// Exe'nin yanindaki aegis.ini. Yoksa varsayilanlarla olusturulur.
std::wstring ExePathW();
std::wstring IniPathW();

void LoadConfig();   // aegis::config() doldurur
void SaveConfig();   // aegis::config() yazar

// Autostart/startHidden gibi alanlari da iceren tam snapshot:
Config SnapshotConfig();
void   PublishConfigSnapshot(const Config& c);

} // namespace aegis

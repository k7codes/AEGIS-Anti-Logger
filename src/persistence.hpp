#pragma once
#include <string>

namespace aegis::persistence {

bool IsStartupRegistered(bool* ok);                    // HKCU Run'da kayit var mi
bool SetStartup(bool on, std::wstring* err = nullptr); // kayit ekle/kaldir
void SyncStartup();                                    // ini config'ine gore kaydi duzelt

} // namespace aegis::persistence

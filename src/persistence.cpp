#include "persistence.hpp"
#include "config_store.hpp"
#include <cwchar>

namespace aegis::persistence {

namespace {
constexpr wchar_t kRunKey[]  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"AEGIS Anti Logger";
}

static LONG QueryOnce(std::wstring* data) {
    HKEY h = nullptr;
    LONG st = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &h);
    if (st != ERROR_SUCCESS) return st;
    DWORD type = 0, cb = 0;
    if (data) {
        st = RegQueryValueExW(h, kValueName, nullptr, &type, nullptr, &cb);
        if (st == ERROR_SUCCESS && type == REG_SZ) {
            std::wstring v(cb / sizeof(wchar_t), L'\0');
            st = RegQueryValueExW(h, kValueName, nullptr, nullptr,
                                  reinterpret_cast<BYTE*>(v.data()), &cb);
            if (st == ERROR_SUCCESS) {
                while (!v.empty() && v.back() == L'\0') v.pop_back();
                *data = std::move(v);
            }
        }
    }
    RegCloseKey(h);
    return st;
}

bool IsStartupRegistered(bool* ok) {
    std::wstring v;
    LONG st = QueryOnce(&v);
    if (ok) *ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    if (st != ERROR_SUCCESS) return false;
    // kendi exe'mizden mi basliyor? ("...exe" --silent" gibi parametre olabilir)
    std::wstring want = L"\"" + ExePathW() + L"\"";
    if (v.size() < want.size()) return false;
    // kaba ama bagimsiz buyuk/kucuk harf duyarsiz on-ek kontrolu
    for (size_t i = 0; i < want.size(); ++i) {
        wchar_t a = towlower(v[i]), b = towlower(want[i]);
        if (a != b) return false;
    }
    return true;
}

bool SetStartup(bool on, std::wstring* err) {
    auto fail = [&](LONG st) {
        if (err) {
            wchar_t msg[256] = {};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, (DWORD)st, 0, msg, 255, nullptr);
            *err = msg;
        }
        return false;
    };
    HKEY h = nullptr;
    LONG st = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                              KEY_SET_VALUE, nullptr, &h, nullptr);
    if (st != ERROR_SUCCESS) return fail(st);
    BOOL good = TRUE;
    if (on) {
        std::wstring data = L"\"" + ExePathW() + L"\" --silent";
        good = RegSetValueExW(h, kValueName, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(data.c_str()),
                              (DWORD)((data.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LONG del = RegDeleteValueW(h, kValueName);
        good = (del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(h);
    if (!good && err) *err = L"RegSetValueExW/RegDeleteValueW basarisiz";
    return good != FALSE;
}

void SyncStartup() {
    Config c = SnapshotConfig();
    bool ok = false;
    bool reg = IsStartupRegistered(&ok);
    if (ok && reg != c.autostart)
        SetStartup(c.autostart, nullptr);
}

} // namespace aegis::persistence

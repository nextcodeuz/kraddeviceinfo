// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - win32 common helpers implementation
#include "wincompat.h"

#ifdef _WIN32

#include <objbase.h>
#include <shlwapi.h>
#include <psapi.h>
#include <powrprof.h>

#pragma comment(lib, "shlwapi.lib")

namespace krad {
namespace win {

// ---------------------------------------------------------------- strings
std::string wide_to_utf8(const wchar_t* s, int len) {
    if (!s || len == 0) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(size_t(need), 0);
    WideCharToMultiByte(CP_UTF8, 0, s, len, &out[0], need, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(size_t(need), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &out[0], need);
    return out;
}

// ---------------------------------------------------------------- registry
static bool reg_open(HKEY root, const std::wstring& subkey, REGSAM wow64, HKEY& hk) {
    LONG r = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | wow64, &hk);
    return r == ERROR_SUCCESS;
}

bool reg_read_string(HKEY root, const std::wstring& subkey,
                     const std::wstring& value, std::string& out, REGSAM wow64) {
    HKEY hk;
    if (!reg_open(root, subkey, wow64, hk)) return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(hk, value.c_str(), nullptr, &type, nullptr, &size);
    bool ok = false;
    if (r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size >= 2) {
        std::vector<wchar_t> buf(size / 2 + 1, 0);
        r = RegQueryValueExW(hk, value.c_str(), nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf.data()), &size);
        if (r == ERROR_SUCCESS) { out = wide_to_utf8(buf.data()); ok = true; }
    }
    RegCloseKey(hk);
    return ok;
}

template <class T>
static bool reg_read_int(HKEY root, const std::wstring& subkey,
                         const std::wstring& value, T& out, REGSAM wow64) {
    HKEY hk;
    if (!reg_open(root, subkey, wow64, hk)) return false;
    DWORD type = 0, size = sizeof(DWORD), data = 0;
    LONG r = RegQueryValueExW(hk, value.c_str(), nullptr, &type,
                              reinterpret_cast<LPBYTE>(&data), &size);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS ||
        (type != REG_DWORD && type != REG_QWORD && type != REG_BINARY)) return false;
    out = T(data);
    return true;
}

bool reg_read_u32(HKEY root, const std::wstring& subkey,
                  const std::wstring& value, std::uint32_t& out, REGSAM wow64) {
    return reg_read_int(root, subkey, value, out, wow64);
}
bool reg_read_u64(HKEY root, const std::wstring& subkey,
                  const std::wstring& value, std::uint64_t& out, REGSAM wow64) {
    HKEY hk;
    if (!reg_open(root, subkey, wow64, hk)) return false;
    DWORD type = 0, size = sizeof(std::uint64_t);
    std::uint64_t data = 0;
    LONG r = RegQueryValueExW(hk, value.c_str(), nullptr, &type,
                              reinterpret_cast<LPBYTE>(&data), &size);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || (type != REG_QWORD && type != REG_DWORD)) return false;
    out = data;
    return true;
}

bool reg_enum_subkeys(HKEY root, const std::wstring& subkey,
                      std::vector<std::wstring>& out, REGSAM wow64) {
    HKEY hk;
    if (!reg_open(root, subkey, wow64, hk)) return false;
    DWORD idx = 0;
    wchar_t name[MAX_PATH + 2];
    while (true) {
        DWORD nlen = MAX_PATH + 1;
        LONG r = RegEnumKeyExW(hk, idx++, name, &nlen, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        out.push_back(name);
        if (out.size() > 4096) break;             // paranoia cap
    }
    RegCloseKey(hk);
    return true;
}

bool reg_enum_values(HKEY root, const std::wstring& subkey,
                     std::vector<std::pair<std::wstring, std::string>>& out,
                     REGSAM wow64) {
    HKEY hk;
    if (!reg_open(root, subkey, wow64, hk)) return false;
    DWORD idx = 0;
    wchar_t vname[16384];
    while (true) {
        DWORD nlen = 16383, type = 0, dsize = 0;
        LONG r = RegEnumValueW(hk, idx++, vname, &nlen, nullptr, &type, nullptr, &dsize);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) break;
        std::string val;
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            std::vector<wchar_t> buf(dsize / 2 + 1, 0);
            DWORD sz2 = dsize;
            if (RegQueryValueExW(hk, vname, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf.data()), &sz2) == ERROR_SUCCESS)
                val = wide_to_utf8(buf.data());
        } else if (type == REG_DWORD) {
            DWORD d = 0; DWORD sz2 = sizeof(d);
            if (RegQueryValueExW(hk, vname, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(&d), &sz2) == ERROR_SUCCESS) {
                char tmp[16]; snprintf(tmp, sizeof tmp, "%lu", (unsigned long)d); val = tmp;
            }
        } else {
            val = "<binary>";
        }
        out.emplace_back(vname, val);
        if (out.size() > 2048) break;
    }
    RegCloseKey(hk);
    return true;
}

// ---------------------------------------------------------------- dynload
static FARPROC proc(LPCSTR mod, LPCSTR fn) {
    HMODULE m = GetModuleHandleA(mod);
    if (!m) m = LoadLibraryA(mod);
    return m ? GetProcAddress(m, fn) : nullptr;
}

RtlGetVersion_t rtl_get_version() {
    static RtlGetVersion_t f =
        reinterpret_cast<RtlGetVersion_t>(proc("ntdll.dll", "RtlGetVersion"));
    return f;
}

NtQuerySystemInformation_t nt_query_system_information() {
    static NtQuerySystemInformation_t f =
        reinterpret_cast<NtQuerySystemInformation_t>(
            proc("ntdll.dll", "NtQuerySystemInformation"));
    return f;
}

GetTickCount64_t get_tick_count64_fn() {
    static GetTickCount64_t f =
        reinterpret_cast<GetTickCount64_t>(proc("kernel32.dll", "GetTickCount64"));
    return f;
}

GetIfTable2_t get_if_table2_fn() {
    static GetIfTable2_t f =
        reinterpret_cast<GetIfTable2_t>(proc("iphlpapi.dll", "GetIfTable2"));
    return f;
}

FreeMibTable_t free_mib_table_fn() {
    static FreeMibTable_t f =
        reinterpret_cast<FreeMibTable_t>(proc("iphlpapi.dll", "FreeMibTable"));
    return f;
}

CallNtPowerInformation_t call_nt_power_information_fn() {
    static CallNtPowerInformation_t f =
        reinterpret_cast<CallNtPowerInformation_t>(
            proc("powrprof.dll", "CallNtPowerInformation"));
    return f;
}

BOOL has_dpi_manifest_ok() { return TRUE; }

// ---------------------------------------------------------------- COM scope
ComScope::ComScope() {
    // apartment choice: MTA plays nicer with WMI in worker threads
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    initialized_ = SUCCEEDED(hr);
}

ComScope::~ComScope() {
    if (initialized_) CoUninitialize();
}

bool is_wow64_process() {
    BOOL wow = FALSE;
    typedef BOOL (WINAPI *IsWow64Process_t)(HANDLE, PBOOL);
    static IsWow64Process_t f = reinterpret_cast<IsWow64Process_t>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process"));
    if (f) f(GetCurrentProcess(), &wow);
    return wow != FALSE;
}

// ---------------------------------------------------------------- time
std::uint64_t now_epoch_ms() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart - 116444736000000000ULL) / 10000ULL;
}

std::uint64_t now_unix_sec() { return now_epoch_ms() / 1000ULL; }

std::string ft_to_str(const FILETIME& ft) {
    SYSTEMTIME st; FILETIME lft = ft;
    FileTimeToLocalFileTime(&ft, &lft);
    if (!FileTimeToSystemTime(&lft, &st)) return "-";
    wchar_t buf[128];
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, buf, 96);
    std::wstring ds(buf);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, buf, 96);
    return wide_to_utf8(ds + L" " + buf);
}

std::string epoch_to_str(std::uint64_t unix_sec) {
    if (!unix_sec) return "-";
    ULARGE_INTEGER u; u.QuadPart = unix_sec * 10000000ULL + 116444736000000000ULL;
    FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    return ft_to_str(ft);
}

// ---------------------------------------------------------------- firmware
bool uefi_firmware() {
    // Documented trick: on UEFI systems the call fails with ERROR_NOACCESS or
    // succeeds; on legacy BIOS it returns ERROR_INVALID_FUNCTION.
    char b[4];
    SetLastError(0);
    GetFirmwareEnvironmentVariableW(L"", L"{00000000-0000-0000-0000-000000000000}",
                                    b, sizeof b);
    DWORD e = GetLastError();
    return !(e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED);
}

} // namespace win
} // namespace krad

#endif // _WIN32

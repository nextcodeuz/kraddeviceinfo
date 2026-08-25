// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - win32 dynamic imports & small helpers (compile: Win7 floor,
// runtime: graceful degradation down to XP-era APIs where possible).
#pragma once

#ifdef _WIN32

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace krad {
namespace win {

// ---------------- string conversion ----------------
std::string  wide_to_utf8(const wchar_t* s, int len = -1);
std::wstring utf8_to_wide(const std::string& s);
inline std::string wide_to_utf8(const std::wstring& s) { return wide_to_utf8(s.c_str(), int(s.size())); }

// ---------------- registry ----------------
bool reg_read_string(HKEY root, const std::wstring& subkey,
                     const std::wstring& value, std::string& out,
                     REGSAM wow64 = 0);
bool reg_read_u32(HKEY root, const std::wstring& subkey,
                  const std::wstring& value, std::uint32_t& out,
                  REGSAM wow64 = 0);
bool reg_read_u64(HKEY root, const std::wstring& subkey,
                  const std::wstring& value, std::uint64_t& out,
                  REGSAM wow64 = 0);
bool reg_enum_subkeys(HKEY root, const std::wstring& subkey,
                      std::vector<std::wstring>& out, REGSAM wow64 = 0);
bool reg_enum_values(HKEY root, const std::wstring& subkey,
                     std::vector<std::pair<std::wstring, std::string>>& out,
                     REGSAM wow64 = 0);   // value name -> data (sz/dword coerced)

// ---------------- dynamic imports ----------------
// ntdll
typedef LONG (WINAPI *RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
RtlGetVersion_t rtl_get_version();
typedef ULONG (WINAPI *NtQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);
NtQuerySystemInformation_t nt_query_system_information();
// kernel32
typedef ULONGLONG (WINAPI *GetTickCount64_t)(void);
GetTickCount64_t get_tick_count64_fn();
BOOL has_dpi_manifest_ok();   // trivial always-true placeholder for future

// iphlpapi (Vista+ row2 APIs)
typedef struct netio_stub* PMIB_IF_TABLE2_STUB;
typedef ULONG (WINAPI *GetIfTable2_t)(void**);
typedef void  (WINAPI *FreeMibTable_t)(void*);
GetIfTable2_t  get_if_table2_fn();
FreeMibTable_t free_mib_table_fn();

// RAII COM initializer for worker threads (WMI / MMDevice need it).
// Safe to nest; no-ops if COM is already initialized in another mode.
class ComScope {
public:
    ComScope();
    ~ComScope();
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    bool initialized_ = false;
};

// pdh (English counter API, Vista+)
namespace pdh {
    bool load();
    void unload();
    struct QueryImpl;                     // opaque
    bool collect(QueryImpl* q);           // pump counters (call each tick)
    QueryImpl* open_query();
    bool add_english_counter(QueryImpl* q, const wchar_t* path, void** handle);
    bool fmt_double(void* handle, double& out);
    bool enum_object_instances(const wchar_t* obj,
                               std::vector<std::wstring>& inst);
    bool add_localized_counter(QueryImpl* q, const wchar_t* path, void** h);
    void close_query(QueryImpl*& q);
}

// powrprof
typedef LONG (WINAPI *CallNtPowerInformation_t)(ULONG, PVOID, ULONG, PVOID, ULONG);
CallNtPowerInformation_t call_nt_power_information_fn();

// ---------------- misc helpers ----------------
std::string ft_to_str(const FILETIME& ft);            // localized datetime
std::string epoch_to_str(std::uint64_t unix_sec);
std::uint64_t now_unix_sec();
std::uint64_t now_epoch_ms();
bool is_wow64_process();
bool uefi_firmware();                                  // true = UEFI boot
} // namespace win
} // namespace krad

#endif // _WIN32

// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - software inventory (win32): apps, startup, services
#include "../../core/collect.h"
#include "wincompat.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <algorithm>
#include <shlobj.h>

namespace krad {
namespace collect {

using namespace krad::win;

static const wchar_t* UNINSTALL_PATHS[] = {
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",   // +HKCU variant
};

std::vector<InstalledApp> installed_apps() {
    std::vector<InstalledApp> out;
    std::map<std::string, bool> seen;

    auto scan = [&](HKEY root, const std::wstring& base, REGSAM wow) {
        std::vector<std::wstring> keys;
        reg_enum_subkeys(root, base, keys, wow);
        for (auto& k : keys) {
            InstalledApp a;
            reg_read_string(root, base + L"\\" + k, L"DisplayName", a.name);
            if (a.name.empty()) continue;
            if (seen.count(lower_copy(a.name))) continue;
            seen[lower_copy(a.name)] = true;

            reg_read_string(root, base + L"\\" + k, L"DisplayVersion", a.version);
            reg_read_string(root, base + L"\\" + k, L"Publisher", a.publisher);
            reg_read_string(root, base + L"\\" + k, L"InstallLocation", a.location);
            reg_read_string(root, base + L"\\" + k, L"InstallDate", a.install_date);
            std::uint32_t kb = 0;
            if (reg_read_u32(root, base + L"\\" + k, L"EstimatedSize", kb) && kb)
                a.size_str = format_bytes(std::uint64_t(kb) * 1024ULL);
            out.push_back(std::move(a));
            if (out.size() > 2048) return;
        }
    };

    scan(HKEY_LOCAL_MACHINE, UNINSTALL_PATHS[0], KEY_WOW64_64KEY);
    scan(HKEY_LOCAL_MACHINE, UNINSTALL_PATHS[1], KEY_WOW64_64KEY);
    scan(HKEY_LOCAL_MACHINE, UNINSTALL_PATHS[0], KEY_WOW64_32KEY);
    scan(HKEY_CURRENT_USER,  UNINSTALL_PATHS[2], 0);

    std::sort(out.begin(), out.end(),
              [](const InstalledApp& a, const InstalledApp& b) {
                  return lower_copy(a.name) < lower_copy(b.name);
              });
    return out;
}

std::vector<StartupEntry> startup_entries() {
    std::vector<StartupEntry> out;
    std::map<std::string, bool> seen;

    auto add = [&](const std::string& name, const std::string& cmd,
                   const std::string& loc) {
        if (name.empty() || cmd.empty()) return;
        std::string id = lower_copy(name) + "|" + lower_copy(cmd);
        if (seen.count(id)) return;
        seen[id] = true;
        StartupEntry e;
        e.name = name; e.command = cmd; e.location = loc; e.state = "-";
        out.push_back(std::move(e));
    };

    struct RunKey { HKEY root; const wchar_t* path; REGSAM wow; const char* label; };
    static const RunKey rks[] = {
        {HKEY_CURRENT_USER,  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, "HKCU Run"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                KEY_WOW64_64KEY, "HKLM Run"},
        {HKEY_LOCAL_MACHINE,
         L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
         0, "HKLM Run (32-bit)"},
    };
    for (auto& rk : rks) {
        std::vector<std::pair<std::wstring, std::string>> vals;
        reg_enum_values(rk.root, rk.path, vals, rk.wow);
        for (auto& v : vals)
            add(win::wide_to_utf8(v.first), v.second, rk.label);
    }

    // startup folders
    for (int csidl : {CSIDL_STARTUP, CSIDL_COMMON_STARTUP}) {
        wchar_t path[MAX_PATH];
        if (!SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, path))
            continue;
        std::wstring pattern = std::wstring(path) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                StartupEntry e;
                e.name = win::wide_to_utf8(fd.cFileName);
                e.command = win::wide_to_utf8(
                    std::wstring(path) + L"\\" + fd.cFileName);
                e.location = csidl == CSIDL_STARTUP ? "Startup folder"
                                                    : "Common startup folder";
                e.state = "-";
                out.push_back(std::move(e));
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}

std::vector<ServiceEntry> services() {
    std::vector<ServiceEntry> out;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return out;

    DWORD need = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_STATE_ALL, nullptr, 0, &need, &count,
                          &resume, nullptr);
    std::vector<char> buf(need + sizeof(DWORD));
    LPENUM_SERVICE_STATUS_PROCESSW list =
        reinterpret_cast<LPENUM_SERVICE_STATUS_PROCESSW>(buf.data());
    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                              SERVICE_STATE_ALL, reinterpret_cast<LPBYTE>(list),
                              DWORD(buf.size()), &need, &count, &resume,
                              nullptr)) {
        for (DWORD i = 0; i < count; ++i) {
            ServiceEntry s;
            s.name         = win::wide_to_utf8(list[i].lpServiceName);
            s.display_name = win::wide_to_utf8(list[i].lpDisplayName);
            switch (list[i].ServiceStatusProcess.dwCurrentState) {
            case SERVICE_RUNNING:     s.state = "Running"; break;
            case SERVICE_STOPPED:     s.state = "Stopped"; break;
            case SERVICE_START_PENDING:  s.state = "Starting"; break;
            case SERVICE_STOP_PENDING:   s.state = "Stopping"; break;
            case SERVICE_PAUSE_PENDING:  s.state = "Pausing"; break;
            case SERVICE_PAUSED:         s.state = "Paused"; break;
            default:                     s.state = "?";
            }
            // start type via QueryServiceConfig
            SC_HANDLE sh = OpenServiceW(scm, list[i].lpServiceName, SERVICE_QUERY_CONFIG);
            if (sh) {
                QUERY_SERVICE_CONFIGW qsc_buf;
                QUERY_SERVICE_CONFIGW* qsc = &qsc_buf;
                BYTE raw[4096]; DWORD n2 = 0;
                if (QueryServiceConfigW(sh,
                        reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(raw), sizeof raw, &n2)) {
                    qsc = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(raw);
                    switch (qsc->dwStartType) {
                    case SERVICE_AUTO_START:  s.start_mode = "Automatic"; break;
                    case SERVICE_BOOT_START:  s.start_mode = "Boot"; break;
                    case SERVICE_SYSTEM_START:s.start_mode = "System"; break;
                    case SERVICE_DEMAND_START:s.start_mode = "Manual"; break;
                    case SERVICE_DISABLED:    s.start_mode = "Disabled"; break;
                    default:                  s.start_mode = "Auto (delayed)";
                    }
                }
                CloseServiceHandle(sh);
            }
            out.push_back(std::move(s));
        }
        std::sort(out.begin(), out.end(), [](const ServiceEntry& a,
                                             const ServiceEntry& b) {
            return lower_copy(a.name) < lower_copy(b.name);
        });
    }
    CloseServiceHandle(scm);
    return out;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - OS / Computer / BIOS collectors (win32)
#include "../../core/collect.h"
#include "wincompat.h"
#include "wmi_helper.h"
#include "../../core/util.h"
#include "smbios.h"

#ifdef _WIN32

#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <psapi.h>

namespace krad {
namespace collect {

using win::wide_to_utf8;
using win::reg_read_u32;
using win::reg_read_string;
using win::reg_enum_subkeys;
using win::reg_enum_values;

static void get_version(RTL_OSVERSIONINFOW& v) {
    ZeroMemory(&v, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    auto f = win::rtl_get_version();
    if (f) f(&v);
    else {
        OSVERSIONINFOEXW o; ZeroMemory(&o, sizeof o); o.dwOSVersionInfoSize = sizeof o;
        // GetVersionEx lies on Win8.1+ but keeps ancient toolchains happy
        #pragma warning(suppress: 4996)
        GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&o));
        v.dwMajorVersion = o.dwMajorVersion;
        v.dwMinorVersion = o.dwMinorVersion;
        v.dwBuildNumber  = o.dwBuildNumber;
    }
}

OsInfo os_info() {
    OsInfo o;
    RTL_OSVERSIONINFOW v; get_version(v);

    char mb[32];
    snprintf(mb, sizeof mb, "%lu", (unsigned long)v.dwMajorVersion);
    o.version_major = mb;
    snprintf(mb, sizeof mb, "%lu", (unsigned long)v.dwMinorVersion);
    o.version_minor = mb;

    std::uint32_t ubr = 0;
    reg_read_u32(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", ubr);

    snprintf(mb, sizeof mb, "%lu.%lu",
             (unsigned long)v.dwBuildNumber, (unsigned long)ubr);
    o.build_number = mb;
    o.build_string = "Build " + o.build_number;

    reg_read_string(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", o.product_name);
    // Win11 reports ProductName "Windows 10..." - fix by build number
    if (v.dwBuildNumber >= 22000 && contains_ci(o.product_name, "Windows 10")) {
        std::string p = o.product_name;
        const char* w10 = "Windows 10";
        size_t pos = lower_copy(p).find("windows 10");
        if (pos != std::string::npos)
            o.product_name = p.substr(0, pos) + "Windows 11" +
                             p.substr(pos + strlen(w10));
    }

    reg_read_string(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"EditionID", o.edition);
    reg_read_string(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion",
        o.display_version);
    if (o.display_version.empty())
        reg_read_string(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ReleaseId",
            o.display_version);

    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: o.architecture = "x86_64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: o.architecture = "ARM64";  break;
    case PROCESSOR_ARCHITECTURE_INTEL: o.architecture = "x86";    break;
    default: o.architecture = "Unknown";
    }
    o.is_64bit = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                 si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
    o.is_wow64 = win::is_wow64_process();

    wchar_t loc[256] = L"";
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLANGUAGE, loc, 255);
    o.locale = wide_to_utf8(loc);

    TIME_ZONE_INFORMATION tzi;
    if (GetTimeZoneInformation(&tzi) != TIME_ZONE_ID_INVALID)
        o.timezone = wide_to_utf8(tzi.StandardName);

    std::uint32_t inst = 0;
    if (reg_read_u32(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"InstallDate", inst))
        o.install_date = win::epoch_to_str(inst);

    ULONGLONG tick = GetTickCount();
    if (auto f = win::get_tick_count64_fn()) tick = f();
    o.uptime_sec = std::uint32_t(tick / 1000ULL);
    o.last_boot  = win::epoch_to_str(win::now_unix_sec() - o.uptime_sec);

    PERFORMANCE_INFORMATION pi; pi.cb = sizeof pi;
    if (GetPerformanceInfo(&pi, sizeof pi)) {
        o.process_count = std::uint32_t(pi.ProcessCount);
        o.thread_count  = std::uint32_t(pi.ThreadCount);
        o.handle_count  = std::uint32_t(pi.HandleCount);
    }

    reg_read_string(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductId", o.product_id);
    reg_read_string(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"RegisteredOwner",
        o.registered_owner);

    wmi::Session s;
    if (s.connect()) {
        std::vector<std::vector<std::string>> rows;
        s.query(L"SELECT HotFixID FROM Win32_QuickFixEngineering",
                {L"HotFixID"}, rows);
        o.hotfix_count = unsigned(rows.size());
    }
    return o;
}

ComputerInfo computer_info() {
    ComputerInfo c;
    wchar_t buf[512] = L""; ULONG blen = 512;

    if (GetComputerNameExW(ComputerNameDnsHostname, buf, &blen))
        c.hostname = wide_to_utf8(buf);
    blen = 512;
    if (GetComputerNameExW(ComputerNameDnsDomain, buf, &blen) && buf[0])
        c.domain = wide_to_utf8(buf);
    else {
        blen = 512;
        if (GetComputerNameExW(ComputerNameNetBIOS, buf, &blen))
            c.workgroup = wide_to_utf8(buf);
    }
    DWORD nlen = 512;
    if (GetUserNameW(buf, &nlen)) c.username = wide_to_utf8(buf);

    wmi::Session s;
    if (s.connect()) {
        std::vector<std::string> row;
        if (s.query_one(L"SELECT Manufacturer, Model, SystemFamily FROM Win32_ComputerSystem",
                        {L"Manufacturer", L"Model", L"SystemFamily"}, row)) {
            c.manufacturer = row[0]; c.model = row[1]; c.system_family = row[2];
        }
        if (s.query_one(L"SELECT UUID FROM Win32_ComputerSystemProduct",
                        {L"UUID"}, row))
            c.uuid = row[0];

        std::vector<std::vector<std::string>> rows;
        if (s.query(L"SELECT ChassisTypes FROM Win32_SystemEnclosure",
                    {L"ChassisTypes"}, rows) && !rows.empty() && !rows[0][0].empty()) {
            static const char* names[] = {"Other","Unknown","Desktop","Low-profile Desktop",
                "Pizza Box","Mini Tower","Tower","Portable","Laptop","Notebook",
                "Hand Held","Docking Station","All in One","Sub Notebook","Space-saving",
                "Lunch Box","Main System Chassis","Expansion Chassis","Sub Chassis",
                "Bus Expansion Chassis","Peripheral Chassis","Storage Chassis",
                "Rack Mount Chassis","Sealed-case PC","Multi-system","CompactPCI",
                "AdvancedTCA","Blade","Blade Enclosure","Tablet","Convertible","Detachable"};
            int t = atoi(rows[0][0].c_str());
            c.chassis_type = (t >= 1 && t <= 32) ? names[t - 1] : rows[0][0];
        }
    }
    if (c.manufacturer.empty() || lower_copy(c.manufacturer) == "system manufacturer")
        c.manufacturer = "-";
    return c;
}

BiosInfo bios_info() {
    BiosInfo b;
    b.mode = win::uefi_firmware() ? "UEFI" : "Legacy BIOS";

    std::uint32_t sb = 0;
    if (reg_read_u32(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
            L"UEFISecureBoot", sb))
        b.secure_boot = sb ? "Enabled" : "Disabled";
    else
        b.secure_boot = b.mode == "UEFI" ? "Unknown" : "Not supported (Legacy)";

    wmi::Session s;
    if (s.connect()) {
        std::vector<std::string> row;
        if (s.query_one(L"SELECT Manufacturer, Name, SMBIOSBIOSVersion, "
                        L"ReleaseDate FROM Win32_BIOS",
                        {L"Manufacturer", L"Name", L"SMBIOSBIOSVersion",
                         L"ReleaseDate"}, row)) {
            b.vendor  = row[0];
            b.version = row[2].empty() ? row[1] : row[1] + " (" + row[2] + ")";
            // WMI date format yyyymmddHHMMSS...
            if (row[3].size() >= 8)
                b.date = row[3].substr(0,4)+"-"+row[3].substr(4,2)+"-"+row[3].substr(6,2);
        }
        if (s.query_one(L"SELECT Manufacturer, Product, Version, SerialNumber "
                        L"FROM Win32_BaseBoard",
                        {L"Manufacturer", L"Product", L"Version", L"SerialNumber"}, row)) {
            b.baseboard_manufacturer = row[0];
            b.baseboard_product      = row[1];
            b.baseboard_version      = row[2];
            b.baseboard_serial       = row[3];
        }
    }
    return b;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

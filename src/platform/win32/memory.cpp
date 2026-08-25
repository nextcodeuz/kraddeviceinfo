// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - memory collector (win32)
#include "../../core/collect.h"
#include "wincompat.h"
#include "wmi_helper.h"
#include "smbios.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <psapi.h>

namespace krad {
namespace collect {

using win::wide_to_utf8;

static void trim(MemoryModule& m) {
    m.slot         = trim_copy(m.slot);
    m.manufacturer = trim_copy(m.manufacturer);
    m.part_number  = trim_copy(m.part_number);
    if (contains_ci(m.manufacturer, "unknown") || m.manufacturer.size() == 8 &&
        upper_copy(m.manufacturer) == m.manufacturer && m.manufacturer[0] == '0')
        m.manufacturer.clear();
    if (m.part_number.find("0000") == 0) m.part_number.clear();
}

MemoryInfo memory_info() {
    MemoryInfo m;

    MEMORYSTATUSEX ms; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        m.total_phys     = ms.ullTotalPhys;
        m.avail_phys     = ms.ullAvailPhys;
        m.total_virtual  = ms.ullTotalVirtual;
        m.avail_virtual  = ms.ullAvailVirtual;
        m.page_file_total = ms.ullTotalPageFile;
        m.page_file_used  = ms.ullTotalPageFile - ms.ullAvailPageFile;
    }

    PERFORMANCE_INFORMATION pi; pi.cb = sizeof pi;
    if (GetPerformanceInfo(&pi, sizeof pi))
        m.pagesize = std::uint32_t(pi.PageSize);

    wmi::Session s;
    if (s.connect()) {
        std::vector<std::vector<std::string>> rows;

        // SPD data from SMBIOS via WMI (fallback when raw table unavailable)
        const std::vector<std::wstring> props{
            L"DeviceLocator", L"Capacity", L"Speed", L"ConfiguredClockSpeed",
            L"SMBIOSMemoryType", L"FormFactor", L"Manufacturer",
            L"PartNumber", L"SerialNumber", L"TypeDetail"};
        s.query(L"SELECT DeviceLocator, Capacity, Speed, ConfiguredClockSpeed, "
                L"SMBIOSMemoryType, FormFactor, Manufacturer, PartNumber, "
                L"SerialNumber, TypeDetail FROM Win32_PhysicalMemory", props, rows);
        for (auto& r : rows) {
            // merge into smbios-provided modules by slot name
            bool merged = false;
            for (auto& mod : m.modules) {
                if (!mod.slot.empty() && mod.slot == r[0]) {
                    std::uint64_t cap = strtoull(r[1].c_str(), nullptr, 10);
                    if (cap) mod.capacity_bytes = cap;
                    std::uint32_t cfg = std::uint32_t(atol(r[3].c_str()));
                    if (cfg) mod.speed_mtps = cfg;
                    merged = true;
                    break;
                }
            }
            if (merged) continue;

            MemoryModule mod;
            mod.slot          = r[0];
            mod.capacity_bytes= strtoull(r[1].c_str(), nullptr, 10);
            mod.max_speed_mtps= std::uint32_t(atol(r[2].c_str()));
            mod.speed_mtps    = std::uint32_t(atol(r[3].c_str()));
            static const char* types[] = {"Unknown","Other","DRAM","Synchronous DRAM",
                "Cache DRAM","EDO","EDRAM","VRAM","SRAM","RAM","ROM","Flash","EEPROM",
                "FEPROM","EPROM","CDRAM","3DRAM","SDRAM","SGRAM","RDRAM","DDR",
                "DDR2","DDR2 FB-DIMM","","DDR3","","DDR4","","DDR5","LPDDR"};
            int t = atoi(r[4].c_str());
            mod.type = (t >= 0 && t < 30 && *types[t]) ? types[t] :
                       (t == 26 ? "DDR4" : t == 34 ? "DDR5" : "");
            static const char* forms[] = {"","Other","SIP","DIP","ZIP","SOJ",
                "Proprietary","SIMM","DIMM","SO-DIMM","SRIMM","Chip","TSOP",
                "Ball grid array","FB-DIMM","Board","Module-174","RIMM","",
                "SODIMM-DDR4/5","FM-DIMM"};
            int ff = atoi(r[5].c_str());
            mod.form_factor = (ff > 0 && ff < 21) ? forms[ff] : "";
            mod.manufacturer = r[6];
            mod.part_number  = r[7];
            mod.serial       = r[8];
            std::uint32_t td = std::uint32_t(atol(r[9].c_str()));
            mod.ecc = (td & 0x20) ? "ECC" : "";
            trim(mod);
            if (mod.capacity_bytes || !mod.part_number.empty()) {
                m.modules.push_back(std::move(mod));
            }
        }

        std::vector<std::string> row;
        if (s.query_one(L"SELECT MemoryDevices FROM Win32_PhysicalMemoryArray",
                        {L"MemoryDevices"}, row)) {
            m.slots_total = std::uint32_t(atol(row[0].c_str()));
        }
    }
    smbios_enrich_memory(m);
    if (!m.slots_total)
        m.slots_total = std::uint32_t(m.modules.size() ? m.modules.size() : 0);
    m.slots_used = 0;
    for (auto& mod : m.modules) if (mod.capacity_bytes) ++m.slots_used;
    return m;

}

} // namespace collect
} // namespace krad

#endif // _WIN32

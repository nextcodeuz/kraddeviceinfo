// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - GPU collector (win32): DXGI + SetupAPI
#include "../../core/collect.h"
#include "wincompat.h"
#include "wmi_helper.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <initguid.h>
#include <dxgi.h>
#include <setupapi.h>
#include <devguid.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "setupapi.lib")

namespace krad {
namespace collect {

static const char* vendor_name(std::uint32_t id) {
    switch (id) {
    case 0x8086: return "Intel";
    case 0x10DE: return "NVIDIA";
    case 0x1002: case 0x1022: return "AMD";
    case 0x15AD: return "VMware";
    case 0x1AF4: return "Red Hat (virtio)";
    case 0x1B36: return "QEMU";
    case 0x1414: return "Microsoft";
    default:     return "";
    }
}

static std::string hex4(std::uint32_t v) {
    char b[8]; snprintf(b, sizeof b, "0x%04X", v);
    return b;
}

// registry driver info keyed by hardware id
struct DriverInfo { std::string version, date; };

static std::string reg_sz(HKEY hk, const wchar_t* val) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hk, val, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    std::vector<wchar_t> buf(size / 2 + 1, 0);
    if (RegQueryValueExW(hk, val, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(buf.data()), &size) != ERROR_SUCCESS)
        return {};
    return win::wide_to_utf8(buf.data());
}

static void collect_setupapi_gpus(std::vector<GpuInfo>& out) {
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr,
                                       DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA dd; 
    for (DWORD i = 0; ; ++i) {
        ZeroMemory(&dd, sizeof dd);
        dd.cbSize = sizeof dd;
        if (!SetupDiEnumDeviceInfo(di, i, &dd)) break;

        GpuInfo g;
        wchar_t buf[1024]; DWORD sz = 0, type = 0;

        if (SetupDiGetDeviceRegistryPropertyW(di, &dd, SPDRP_DEVICEDESC,
                &type, reinterpret_cast<PBYTE>(buf), sizeof buf, nullptr))
            g.name = win::wide_to_utf8(buf);

        std::string hwid;
        if (SetupDiGetDeviceRegistryPropertyW(di, &dd, SPDRP_HARDWAREID,
                &type, reinterpret_cast<PBYTE>(buf), sizeof buf, &sz))
            hwid = win::wide_to_utf8(buf);

        // parse VEN_xxxx&DEV_xxxx
        auto up = upper_copy(hwid);
        size_t v = up.find("VEN_");
        size_t d = up.find("DEV_");
        if (v != std::string::npos && v + 8 <= up.size())
            g.vendor_id  = "0x" + up.substr(v + 4, 4);
        if (d != std::string::npos && d + 8 <= up.size())
            g.device_id  = "0x" + up.substr(d + 4, 4);
        if (!g.vendor_id.empty()) {
            std::uint32_t vid = std::uint32_t(strtoul(g.vendor_id.c_str(), nullptr, 16));
            const char* vn = vendor_name(vid);
            g.vendor = vn ? vn : "";
        }

        // driver key via SPDRP_DRIVER ("{classguid}\00XX") then registry
        std::string drv;
        if (SetupDiGetDeviceRegistryPropertyW(di, &dd, SPDRP_DRIVER,
                &type, reinterpret_cast<PBYTE>(buf), sizeof buf, nullptr))
            drv = win::wide_to_utf8(buf);
        if (!drv.empty()) {
            std::wstring sub = L"SYSTEM\\CurrentControlSet\\Control\\Class\\" +
                               win::utf8_to_wide(drv);
            HKEY hk;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0,
                              KEY_READ, &hk) == ERROR_SUCCESS) {
                g.driver_version = reg_sz(hk, L"DriverVersion");
                g.driver_date    = reg_sz(hk, L"DriverDate");
                RegCloseKey(hk);
            }
        }
        if (g.name.find(g.vendor) == std::string::npos && !g.vendor.empty())
            g.name = g.vendor + " " + g.name;
        out.push_back(std::move(g));
    }
    SetupDiDestroyDeviceInfoList(di);
}

std::vector<GpuInfo> gpus() {
    std::vector<GpuInfo> out;

    // ---- DXGI adapters (accurate VRAM, vendor ids; Win7+) -------------------
    typedef HRESULT (WINAPI *CreateDXGIFactory1_t)(void*, void**);
    static CreateDXGIFactory1_t create_f1 = reinterpret_cast<CreateDXGIFactory1_t>(
        GetProcAddress(GetModuleHandleA("dxgi.dll"), "CreateDXGIFactory1"));
    IDXGIFactory1* factory = nullptr;
    bool dxgi_ok = false;
    if (create_f1 &&
        SUCCEEDED(create_f1((void*)&IID_IDXGIFactory1, (void**)&factory)))
        dxgi_ok = true;

    std::map<std::wstring, size_t> by_desc;
    if (dxgi_ok) {
        IDXGIAdapter1* ad = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(ad->GetDesc1(&desc))) {
                GpuInfo g;
                g.name       = win::wide_to_utf8(desc.Description);
                g.vram_bytes = desc.DedicatedVideoMemory;
                g.shared_ram_bytes = desc.SharedSystemMemory;
                g.vendor_id  = hex4(desc.VendorId);
                g.device_id  = hex4(desc.DeviceId);
                const char* vn = vendor_name(desc.VendorId);
                g.vendor = vn ? vn : "";
                if (i == 0) g.primary = true;
                g.status = "OK";
                by_desc[std::wstring(win::utf8_to_wide(g.name))] = out.size();
                out.push_back(std::move(g));
            }
            ad->Release();
        }
        factory->Release();
    }

    // ---- SetupAPI enumeration adds driver info / covers XP -----------------
    std::vector<GpuInfo> setup_gpus;
    collect_setupapi_gpus(setup_gpus);

    for (auto& sg : setup_gpus) {
        bool merged = false;
        for (auto& og : out) {
            if (!sg.vendor_id.empty() && sg.vendor_id == og.vendor_id &&
                !sg.device_id.empty() && sg.device_id == og.device_id) {
                if (og.name.empty()) og.name = sg.name;
                if (og.driver_version.empty()) og.driver_version = sg.driver_version;
                if (og.driver_date.empty())    og.driver_date    = sg.driver_date;
                merged = true;
                break;
            }
        }
        if (!merged)
            out.push_back(std::move(sg));
    }

    // dedupe identical names lacking data
    std::vector<GpuInfo> clean;
    for (auto& g : out) {
        bool dup = false;
        for (auto& c : clean)
            if (!c.name.empty() && c.name == g.name &&
                c.driver_version == g.driver_version) { dup = true; break; }
        if (!dup) clean.push_back(std::move(g));
    }

    // WMI fallback fills blanks (video mode etc.)
    wmi::Session s;
    if (s.connect() && !clean.empty()) {
        std::vector<std::vector<std::string>> rows;
        s.query(L"SELECT Name, VideoModeDescription, Status, VideoProcessor "
                L"FROM Win32_VideoController",
                {L"Name", L"VideoModeDescription", L"Status", L"VideoProcessor"},
                rows);
        for (auto& r : rows) {
            for (auto& g : clean) {
                if (!contains_ci(g.name, r[0]) && !r[0].empty() &&
                    contains_ci(r[0], g.name.substr(0, std::min<size_t>(20, g.name.size())))) {
                    if (g.video_mode.empty()) g.video_mode = r[1];
                    if (g.status.empty())     g.status     = r[2];
                } else if (g.video_mode.empty() && contains_ci(g.name, r[0])) {
                    g.video_mode = r[1];
                    g.status     = r[2];
                }
            }
        }
    }
    for (auto& g : clean) {
        if (g.status.empty()) g.status = "OK";
        if (g.vendor.empty()) g.vendor = "Unknown";
    }
    return clean;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

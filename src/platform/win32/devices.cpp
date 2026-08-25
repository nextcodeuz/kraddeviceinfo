// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - USB / battery / audio collectors (win32)
#include "../../core/collect.h"
#include "wincompat.h"
#include "wmi_helper.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmsystem.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace krad {
namespace collect {

// ---------------------------------------------------------------- USB
std::vector<UsbDevice> usb_devices() {
    std::vector<UsbDevice> out;
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, nullptr, nullptr,
                                       DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;

    SP_DEVINFO_DATA dd;
    for (DWORD i = 0; ; ++i) {
        ZeroMemory(&dd, sizeof dd);
        dd.cbSize = sizeof dd;
        if (!SetupDiEnumDeviceInfo(di, i, &dd)) break;

        UsbDevice u;
        wchar_t buf[1024]; DWORD sz = 0, type = 0;

        auto prop = [&](int which) -> std::string {
            if (SetupDiGetDeviceRegistryPropertyW(di, &dd, which, &type,
                    reinterpret_cast<PBYTE>(buf), sizeof buf, nullptr))
                return win::wide_to_utf8(buf);
            return {};
        };
        u.name        = prop(SPDRP_FRIENDLYNAME);
        u.description = prop(SPDRP_DEVICEDESC);
        if (u.name.empty()) u.name = u.description;
        u.manufacturer= prop(SPDRP_MFG);
        u.location    = prop(SPDRP_LOCATION_INFORMATION);
        std::string hwid = prop(SPDRP_HARDWAREID);
        auto up = upper_copy(hwid.substr(0, hwid.find('\0') == std::string::npos
                                            ? hwid.size() : hwid.find('\0')));
        size_t v = up.find("VID_");
        size_t p2 = up.find("PID_");
        if (v != std::string::npos && v + 8 <= up.size())
            u.vid_pid = "VID " + up.substr(v + 4, 4);
        if (p2 != std::string::npos && p2 + 8 <= up.size())
            u.vid_pid += " PID " + up.substr(p2 + 4, 4);

        // device status
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, dd.DevInst, 0)
                == CR_SUCCESS) {
            if (problem == 0) u.status = "OK";
            else switch (problem) {
            case CM_PROB_DISABLED: u.status = "Disabled"; break;
            case CM_PROB_FAILED_START: u.status = "Cannot start"; break;
            case CM_PROB_NEED_RESTART: u.status = "Restart required"; break;
            default: { char b[24]; snprintf(b, sizeof b, "Problem %lu",
                                            (unsigned long)problem); u.status = b; }
            }
        } else u.status = "?";
        out.push_back(std::move(u));
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}

// ---------------------------------------------------------------- battery
// SYSTEM_BATTERY_INFORMATION via CallNtPowerInformation (layout per docs)
struct SysBatteryInfoRaw {
    unsigned char ac_online, present, charging, discharging, spare[3];
    unsigned char tag;
    std::uint32_t designed_capacity;    // mWh
    std::uint32_t full_charged_capacity;
    std::uint32_t default_alert1, default_alert2;
    std::uint32_t critical_bias;        // mW
    std::uint32_t cycle_count;
};

BatteryInfo battery() {
    BatteryInfo b;

    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps)) return b;
    if (sps.BatteryFlag == 128 || sps.BatteryFlag == BATTERY_FLAG_NO_BATTERY)
        return b;                                    // no battery
    b.present = true;

    b.charge_pct = sps.BatteryLifePercent == 255
                 ? "-" : std::to_string(sps.BatteryLifePercent) + " %";
    b.ac_line = sps.ACLineStatus ? "Online" : "Offline";

    std::string state;
    const bool charging = (sps.BatteryFlag & BATTERY_FLAG_CHARGING) != 0;
    if (charging) state += "Charging ";
    else if (!sps.ACLineStatus) state += "Discharging ";
    if (!charging && sps.ACLineStatus) state += "Idle ";
    b.state = trim_copy(state.empty() ? std::string("Unknown") : state);

    if (sps.BatteryLifeTime != (DWORD)-1 && sps.BatteryLifeTime > 0)
        b.remaining_time = format_duration_sec(sps.BatteryLifeTime);

    auto fn = win::call_nt_power_information_fn();
    if (fn) {
        SysBatteryInfoRaw bi; ZeroMemory(&bi, sizeof bi);
        if (fn(3 /*BatteryInformation*/, nullptr, 0, &bi, sizeof bi) == 0 &&
            bi.designed_capacity > 100) {
            char v1[48], v2[48], v3[16];
            snprintf(v1, sizeof v1, "%.2f Wh", bi.designed_capacity / 1000.0);
            snprintf(v2, sizeof v2, "%.2f Wh", bi.full_charged_capacity / 1000.0);
            b.design_capacity_wh = v1;
            b.full_charge_wh     = v2;
            if (bi.full_charged_capacity) {
                snprintf(v3, sizeof v3, "%.1f %%",
                    100.0 * (double(bi.full_charged_capacity) /
                             double(bi.designed_capacity)));
                b.wear_pct = v3;
            }
            if (bi.cycle_count)
                b.saved_battery_time =
                    std::to_string(bi.cycle_count) + " cycles";
        }
    }

    wmi::Session s;
    if (s.connect()) {
        std::vector<std::string> row;
        if (s.query_one(L"SELECT DesignVoltage, Chemistry, EstimatedChargeRemaining "
                        L"FROM Win32_Battery",
                        {L"DesignVoltage", L"Chemistry",
                         L"EstimatedChargeRemaining"}, row)) {
            int mv = atoi(row[0].c_str());
            if (mv > 0) {
                char v[24];
                snprintf(v, sizeof v, "%.2f V", mv / 1000.0);
                b.voltage_mv = v;
            }
            static const char* chem[] = {"Other","Unknown","Lead Acid","Nickel Cadmium",
                "Nickel Metal Hydride","Lithium-ion","Zinc air","Lithium Polymer"};
            int ch = atoi(row[1].c_str());
            if (ch >= 1 && ch <= 7) b.chemistry = chem[ch];
            else b.chemistry = row[1];
            if (b.charge_pct == "-" && !row[2].empty())
                b.charge_pct = row[2] + " %";
        }
    }
    return b;
}

// ---------------------------------------------------------------- audio
std::vector<AudioDevice> audio_devices() {
    std::vector<AudioDevice> out;
    win::ComScope com;                          // MMDevice needs COM

    // Vista+ MMDevice API
    typedef HRESULT (WINAPI *CoCreateFn)(REFCLSID, LPVOID*, DWORD);
    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                  CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                  reinterpret_cast<void**>(&en));
    if (SUCCEEDED(hr) && en) {
        for (int flow = 0; flow < 2; ++flow) {
            EDataFlow f = flow == 0 ? eRender : eCapture;
            IMMDeviceCollection* col = nullptr;
            if (FAILED(en->EnumAudioEndpoints(f, DEVICE_STATEMASK_ALL, &col))
                || !col) continue;
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n && i < 64; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(col->Item(i, &dev)) || !dev) continue;
                IPropertyStore* ps = nullptr;
                AudioDevice a;
                a.endpoint = flow == 0 ? "Output" : "Input";
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv))) {
                        if (pv.vt == VT_BSTR) a.name = win::wide_to_utf8(pv.bstrVal);
                        PropVariantClear(&pv);
                    }
                    ps->Release();
                }
                DWORD st = 0;
                if (SUCCEEDED(dev->GetState(&st)))
                    a.state = st == DEVICE_STATE_ACTIVE ? "Active" :
                              st == DEVICE_STATE_DISABLED ? "Disabled" :
                              st == DEVICE_STATE_NOTPRESENT ? "Not present" : "Unplugged";
                dev->Release();
                if (!a.name.empty()) out.push_back(std::move(a));
            }
            col->Release();
        }
        en->Release();
        return out;
    }

    // XP fallback: wave devices
    int n = int(waveOutGetNumDevs());
    for (int i = -1; i < n; ++i) {
        WAVEOUTCAPSW cap; 
        bool ok = i < 0 ? false : waveOutGetDevCapsW(UINT(i), &cap, sizeof cap) == MMSYSERR_NOERROR;
        if (ok) {
            AudioDevice a;
            a.endpoint = "Output (legacy)";
            a.name = win::wide_to_utf8(cap.szPname);
            a.state = "Active";
            out.push_back(std::move(a));
        }
    }
    return out;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

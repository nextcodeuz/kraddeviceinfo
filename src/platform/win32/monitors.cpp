// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - display monitors with EDID parsing (win32)
#include "../../core/collect.h"
#include "wincompat.h"
#include "../../core/util.h"

#ifdef _WIN32

namespace krad {
namespace collect {

// ---- EDID ----------------------------------------------------------------
struct EdidData {
    std::string manufacturer;      // "DEL", "AOC"...
    std::uint16_t product_code = 0;
    std::uint32_t serial_num = 0;
    int week = 0, year = 0;
    std::string name;
    std::string serial;
    int pref_w = 0, pref_h = 0;
    double pref_hz = 0.0;
    double gamma = 0.0;
};

static void decode_edid_manufacturer(const std::uint8_t* e, EdidData& d) {
    // 3 x 5-bit letters, big-endian packed
    char m[4];
    m[0] = char('A' + ((e[0] >> 2) & 0x1F) - 1);
    m[1] = char('A' + (((e[0] & 3) << 3) | ((e[1] >> 5) & 7)) - 1);
    m[2] = char('A' + (e[1] & 0x1F) - 1);
    m[3] = 0;
    if (m[0] >= 'A' && m[0] <= 'Z') d.manufacturer = m;
}

static bool parse_descriptor(const std::uint8_t* b, std::string& out) {
    if (b[0] != 0 || b[1] != 0 || b[2] != 0) return false;
    std::uint8_t type = b[3];
    const std::uint8_t* txt = b + 5;
    size_t len = 13;
    out.assign(reinterpret_cast<const char*>(txt),
               strnlen(reinterpret_cast<const char*>(txt), len));
    while (!out.empty() && (out.back() == '\n')) out.pop_back();
    return type >= 0xFF;
}

static bool parse_edid(const std::vector<std::uint8_t>& raw, EdidData& d) {
    if (raw.size() < 128 || raw[0] != 0x00 || raw[1] != 0xFF) return false;

    decode_edid_manufacturer(raw.data() + 8, d);
    d.product_code = std::uint16_t(raw[10] | (raw[11] << 8));
    memcpy(&d.serial_num, raw.data() + 12, 4);
    d.week = raw[16];
    d.year = 1990 + raw[17];
    d.gamma = raw[23] ? (raw[23] + 100) / 100.0 : 0.0;

    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* desc = raw.data() + 54 + i * 18;
        if (desc[0] == 0 && desc[1] == 0 && desc[2] == 0) {
            std::uint8_t type = desc[3];
            if (type == 0xFC) {                       // monitor name
                std::string s;
                parse_descriptor(desc, s);
                if (!s.empty()) d.name = s;
            } else if (type == 0xFF) {                // serial string
                std::string s;
                parse_descriptor(desc, s);
                if (!s.empty()) d.serial = s;
            }
        } else if (!d.pref_w) {                        // preferred timing
            std::uint32_t px10k = desc[2] | (desc[3] << 8);   // pixel clock /10kHz
            d.pref_w = desc[4] | ((desc[6] >> 4) << 8);        // H active
            d.pref_h = desc[7] | ((desc[9] >> 4) << 8);        // V active
            if (px10k && d.pref_w && d.pref_h) {
                std::uint32_t hblank = desc[5] | ((desc[6] & 0xF) << 8);
                std::uint32_t vblank = desc[8] | ((desc[9] & 0xF) << 8);
                std::uint64_t total =
                    std::uint64_t(d.pref_w + hblank) *
                    std::uint64_t(d.pref_h + vblank);
                d.pref_hz = total ? px10k * 10000.0 / double(total) : 60.0;
            } else d.pref_hz = 60.0;
        }
    }
    return true;
}

// registry EDID lookup: EnumDisplayDevices gives PNP id -> Enum\DISPLAY key
static bool load_edid_for(const std::string& device_id,
                          std::vector<std::uint8_t>& raw) {
    // device_id like DISPLAY\DELA0BC\5&2b34f&0&UID267
    auto parts = split_string(device_id, '\\');
    if (parts.size() < 3) return false;
    std::wstring sub = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" +
        win::utf8_to_wide(parts[1]) + L"\\" + win::utf8_to_wide(parts[2]) +
        L"\\Device Parameters";
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, KEY_READ, &hk)
            != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(hk, L"EDID", nullptr, &type, nullptr, &size);
    if (r == ERROR_SUCCESS && type == REG_BINARY && size >= 128 && size <= 512) {
        raw.resize(size);
        r = RegQueryValueExW(hk, L"EDID", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(raw.data()), &size);
    }
    RegCloseKey(hk);
    return r == ERROR_SUCCESS && raw.size() >= 128;
}

std::vector<MonitorInfo> monitors() {
    std::vector<MonitorInfo> out;

    DISPLAY_DEVICEW dd; dd.cb = sizeof dd;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, EDD_GET_DEVICE_INTERFACE_NAME); ++i) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;

        MonitorInfo m;
        m.primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

        DISPLAY_DEVICEW mon; mon.cb = sizeof mon;
        if (EnumDisplayDevicesW(dd.DeviceName, 0, &mon,
                                EDD_GET_DEVICE_INTERFACE_NAME)) {
            m.connection = win::wide_to_utf8(mon.DeviceString);

            // current mode
            DEVMODEW dm; ZeroMemory(&dm, sizeof dm);
            dm.dmSize = sizeof dm;
            if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
                m.current_mode = std::to_string(dm.dmPelsWidth) + "x" +
                                 std::to_string(dm.dmPelsHeight) + " @ " +
                                 std::to_string(dm.dmDisplayFrequency) + " Hz, " +
                                 std::to_string(dm.dmBitsPerPel) + "-bit";

            // largest supported resolution from mode list
            int maxw = 0, maxh = 0;
            for (DWORD mi = 0; EnumDisplaySettingsW(dd.DeviceName, mi, &dm); ++mi) {
                int area = int(dm.dmPelsWidth) * int(dm.dmPelsHeight);
                int best = maxw * maxh;
                if (area > best) { maxw = int(dm.dmPelsWidth); maxh = int(dm.dmPelsHeight); }
            }
            if (maxw > 0)
                m.native_resolution = std::to_string(maxw) + "x" + std::to_string(maxh);

            // EDID from registry
            std::vector<std::uint8_t> raw;
            if (load_edid_for(win::wide_to_utf8(mon.DeviceID), raw)) {
                EdidData ed;
                if (parse_edid(raw, ed)) {
                    if (!ed.name.empty())   m.name = ed.name;
                    if (!ed.manufacturer.empty())
                        m.manufacturer = ed.manufacturer;
                    if (!ed.serial.empty()) m.serial = ed.serial;
                    else if (ed.serial_num)
                        m.serial = "#" + std::to_string(ed.serial_num);
                    if (ed.year) {
                        char y[40];
                        snprintf(y, sizeof y, "week %d of %d", ed.week, ed.year);
                        m.edid_week_year = y;
                    }
                    if (ed.gamma > 0) {
                        char g[16]; snprintf(g, sizeof g, "%.2f", ed.gamma);
                        m.gamma = g;
                    }
                    if (ed.pref_w && ed.pref_h &&
                        ed.pref_w >= maxw) {          // preferred is native
                        char nr[64];
                        snprintf(nr, sizeof nr, "%dx%d @ %.0f Hz",
                                 ed.pref_w, ed.pref_h, ed.pref_hz);
                        m.native_resolution = nr;
                    }
                }
            }
        }
        if (m.name.empty()) {
            m.name = "Display " + std::to_string(out.size() + 1);
        }
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

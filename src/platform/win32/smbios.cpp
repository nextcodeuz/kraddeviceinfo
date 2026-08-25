// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - raw SMBIOS parser via GetSystemFirmwareTable('RSMB')
// Works on XP+ and exposes data WMI often hides (real SPD, UUID order...).
#include "../../core/collect.h"
#include "wincompat.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <map>

namespace krad {
namespace collect {
namespace smbios {

// RAW_SMBIOS_DATA layout (GetSystemFirmwareTable provider 'RSMB'):
// [0] Used20Calling, [1] SMBIOSMajor, [2] SMBIOSMinor, [3] DmiSize hi? ->
// official: BYTE Used20CallingMethod; BYTE SMBIOSMajorVersion;
//          BYTE SMBIOSMinorVersion; BYTE DmiRevision; DWORD Length; BYTE SMBIOSTableData[]
struct RawTable {
    std::uint8_t  used20, major, minor, revision;
    std::uint32_t length;
    // data follows
};

static const std::uint8_t* table_data(const std::vector<std::uint8_t>& buf) {
    return buf.data() + 8;   // after header
}
static size_t table_size(const std::vector<std::uint8_t>& buf) {
    if (buf.size() < 8) return 0;
    std::uint32_t len;
    memcpy(&len, buf.data() + 4, 4);
    return std::min<size_t>(len, buf.size() - 8);
}

struct StructHeader { std::uint8_t type, length; std::uint16_t handle; };

class Parser {
public:
    bool load() {
        std::uint32_t sig = 'R' | ('S' << 8) | ('M' << 16) | ('B' << 24);
        UINT need = GetSystemFirmwareTable(sig, 0, nullptr, 0);
        if (!need) return false;
        buf_.resize(need);
        UINT got = GetSystemFirmwareTable(sig, 0, buf_.data(), need);
        if (got != need || got < 8) { buf_.clear(); return false; }
        major_ = buf_[1]; minor_ = buf_[2];
        pos_ = table_data(buf_);
        end_ = pos_ + table_size(buf_);
        return end_ > pos_;
    }

    void walk() {
        const std::uint8_t* p = pos_;
        while (p + 4 <= end_) {
            auto* h = reinterpret_cast<const StructHeader*>(p);
            if (h->length < 4) break;
            const std::uint8_t* strings = p + h->length;
            // collect string block
            str_off_ = 1;
            str_map_.clear();
            const std::uint8_t* s = strings;
            while (s < end_ && !(s[0] == 0 && s[1] == 0)) {
                const char* start = reinterpret_cast<const char*>(s);
                size_t max_len = size_t(end_ - s);
                std::string val(start, strnlen(start, max_len));
                str_map_[str_off_++] = val;
                s += strlen(start) + 1;
                if (s >= end_) break;
            }
            s += 2;                                   // skip terminator
            dispatch(h, p);
            p = s;
        }
    }

    std::string version() const {
        char b[16]; snprintf(b, sizeof b, "%u.%u", major_, minor_);
        return b;
    }

    // results
    std::string bios_vendor, bios_version, bios_date;
    std::string sys_manufacturer, sys_product, sys_version, sys_serial,
                sys_uuid, sys_sku, sys_family;
    std::string board_manufacturer, board_product, board_version, board_serial;
    std::string cpu_socket, cpu_manufacturer, cpu_version;
    double cpu_max_mhz = 0, cpu_cur_mhz = 0;
    struct MemDev { std::string locator, manufacturer, part, serial, type, form;
                    std::uint64_t size_mb = 0; std::uint32_t speed = 0; };
    std::vector<MemDev> mem_devices;

private:
    std::string str(int idx) const {
        if (idx == 0) return {};
        auto it = str_map_.find(idx);
        return it == str_map_.end() ? std::string() : it->second;
    }
    static std::uint16_t u16(const std::uint8_t* p) { std::uint16_t v; memcpy(&v,p,2); return v; }
    static std::uint32_t u32(const std::uint8_t* p) { std::uint32_t v; memcpy(&v,p,4); return v; }
    static std::uint64_t u64(const std::uint8_t* p) { std::uint64_t v; memcpy(&v,p,8); return v; }

    void dispatch(const StructHeader* h, const std::uint8_t* p);

    std::vector<std::uint8_t> buf_;
    const std::uint8_t* pos_ = nullptr;
    const std::uint8_t* end_ = nullptr;
    int str_off_ = 1;
    std::map<int, std::string> str_map_;
    std::uint8_t major_ = 0, minor_ = 0;
};

static const char* memory_type_name(std::uint8_t t) {
    switch (t) {
    case 0x12: return "DDR";   case 0x13: return "DDR2"; case 0x18: return "DDR3";
    case 0x1A: return "DDR4";  case 0x1B: return "LPDDR"; case 0x1C: return "LPDDR2";
    case 0x1D: return "LPDDR3";case 0x1E: return "LPDDR4";
    case 0x22: return "DDR5";  case 0x23: return "LPDDR5";
    default:   return "";
    }
}
static const char* form_factor_name(std::uint8_t t) {
    switch (t) {
    case 0x08: return "DIMM";  case 0x0C: return "SODIMM"; case 0x0D: return "SRIMM";
    case 0x0F: return "FB-DIMM"; case 0x20: return "Row of chips";
    default:   return "";
    }
}

void Parser::dispatch(const StructHeader* h, const std::uint8_t* p) {
    switch (h->type) {
    case 0: // BIOS
        if (h->length >= 0x18) {
            bios_vendor  = str(p[0x04]);
            bios_version = str(p[0x05]);
            bios_date    = str(p[0x08]);                 // Release Date string
        }
        break;
    case 1: // System
        if (h->length >= 0x19) {
            sys_manufacturer = str(p[0x04]);
            sys_product      = str(p[0x05]);
            sys_version      = str(p[0x06]);
            sys_serial       = str(p[0x07]);
            // UUID: first three groups little-endian per spec
            const std::uint8_t* uu = p + 0x08;
            char ub[40];
            snprintf(ub, sizeof ub,
                "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                uu[3],uu[2],uu[1],uu[0], uu[5],uu[4], uu[7],uu[6],
                uu[8],uu[9],uu[10],uu[11],uu[12],uu[13],uu[14],uu[15]);
            bool all_zero = true, all_ff = true;
            for (int i = 0; i < 16; ++i)
                if (uu[i]) all_zero = false;
                else if (uu[i] != 0xFF) all_ff = false;
            if (!(all_zero || all_ff)) sys_uuid = ub;
            if (h->length >= 0x1B) {
                sys_sku    = str(p[0x19]);
                sys_family = str(p[0x1A]);
            }
        }
        break;
    case 2: // Baseboard
        if (h->length >= 0x08) {
            board_manufacturer = str(p[0x04]);
            board_product      = str(p[0x05]);
            board_version      = str(p[0x06]);
            board_serial       = str(p[0x07]);
        }
        break;
    case 4: // Processor
        if (h->length >= 0x28) {
            cpu_socket       = str(p[0x04]);
            cpu_manufacturer = str(p[0x07]);
            cpu_version      = str(p[0x10]);
            cpu_max_mhz      = u16(p + 0x14);            // Max Speed
            cpu_cur_mhz      = u16(p + 0x16);            // Current Speed
        }
        break;
    case 17: // Memory device (SMBIOS 3.x offsets)
        if (h->length >= 0x28) {
            MemDev md;
            md.locator      = str(p[0x10]);              // Device Locator
            md.manufacturer = str(p[0x17]);
            md.serial       = str(p[0x18]);
            md.part         = str(p[0x1A]);
            std::uint16_t sz = u16(p + 0x0C);            // Size WORD
            if (sz == 0x8000 && h->length >= 0x21)       // Extended Size DWORD @1Dh
                md.size_mb = u32(p + 0x1D) & 0x7FFFFFFFULL;
            else if (sz & 0x8000)                        // bit15 set => MB units
                md.size_mb = sz & 0x7FFF;
            else if (sz)
                md.size_mb = std::uint64_t(sz) * 1024;   // KB units
            md.speed     = u16(p + 0x15) & 0x7FFF;       // Speed (max) @15h
            md.form      = form_factor_name(p[0x0E]);
            md.type      = memory_type_name(p[0x12]);
            if (h->length >= 0x23) {                     // Configured speed @21h
                std::uint16_t cs = u16(p + 0x21) & 0x7FFF;
                if (!md.speed) md.speed = cs;
            }
            if (md.size_mb || !md.manufacturer.empty())
                mem_devices.push_back(std::move(md));
        }
        break;
    default:
        break;
    }
}

} // namespace smbios

// enrichment hooks used by cpu/memory/bios collectors
// (still inside krad::collect)

std::string smbios_version();
void        smbios_enrich_bios(BiosInfo& b);
void        smbios_enrich_cpu(CpuInfo& c);
void        smbios_enrich_memory(MemoryInfo& m);

namespace impl {
void run_all(BiosInfo& b, CpuInfo& c, MemoryInfo& m, std::string& ver) {
    smbios::Parser p;
    if (!p.load()) return;
    ver = p.version();
    p.walk();

    auto set = [](std::string& dst, const std::string& src) {
        if (dst.empty() && !src.empty()) dst = src;
    };
    set(b.vendor, p.bios_vendor);
    set(b.version, p.bios_version);
    set(b.date, p.bios_date);
    set(b.baseboard_manufacturer, p.board_manufacturer);
    set(b.baseboard_product, p.board_product);
    set(b.baseboard_version, p.board_version);
    set(b.baseboard_serial, p.board_serial);
    set(c.socket, p.cpu_socket);
    if (!c.max_clock_mhz && p.cpu_cur_mhz) c.max_clock_mhz = p.cpu_cur_mhz;

    TableItem ti;
    ti.name = "System";
    ti.detail1 = p.sys_manufacturer;
    ti.detail2 = p.sys_product;
    ti.detail3 = p.sys_version;
    if (!ti.detail1.empty() && ti.detail1 != "System Manufacturer")
        b.smbios_extra.push_back(ti);
    ti.name = "System Serial";
    ti.detail1 = p.sys_serial; ti.detail2.clear(); ti.detail3.clear();
    if (!ti.detail1.empty() && lower_copy(ti.detail1) != "none")
        b.smbios_extra.push_back(ti);

    for (auto& md : p.mem_devices) {
        MemoryModule mod;
        mod.slot         = md.locator;
        mod.capacity_bytes = md.size_mb * 1024ULL * 1024ULL;
        mod.max_speed_mtps = md.speed;
        mod.speed_mtps     = md.speed;
        mod.type           = md.type;
        mod.form_factor    = md.form;
        mod.manufacturer   = md.manufacturer;
        mod.part_number    = md.part;
        mod.serial         = md.serial;
        m.modules.push_back(std::move(mod));
        if (mod.capacity_bytes) ++m.slots_used;
    }
}
} // namespace impl

std::string smbios_version() {
    smbios::Parser p;
    return p.load() ? p.version() : std::string();
}
void smbios_enrich_bios(BiosInfo& b) {
    CpuInfo tmp; MemoryInfo tmpm; std::string v;
    impl::run_all(b, tmp, tmpm, v);
}
void smbios_enrich_cpu(CpuInfo& c) {
    BiosInfo tmpb; MemoryInfo tmpm; std::string v;
    impl::run_all(tmpb, c, tmpm, v);
}
void smbios_enrich_memory(MemoryInfo& m) {
    BiosInfo tmpb; CpuInfo tmpc; std::string v;
    impl::run_all(tmpb, tmpc, m, v);
}

} // namespace collect
} // namespace krad

#endif // _WIN32

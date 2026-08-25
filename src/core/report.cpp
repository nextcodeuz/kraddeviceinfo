// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// Aggregate report + flat section view shared by GUI and exporters.
#include "collect.h"
#include "util.h"
#include <krad/model.h>

namespace krad {

static ReportSection sec(const std::string& title, const char* icon,
                         std::vector<KVRow> rows) {
    ReportSection s;
    s.title = title;
    s.icon = icon;
    s.rows = std::move(rows);
    return s;
}

std::vector<ReportSection> DeviceReport::sections() const {
    std::vector<ReportSection> out;
    out.reserve(24);

    out.push_back(sec("Operating System", "os", {
        {"Product", os.product_name},
        {"Edition", os.edition},
        {"Version", os.display_version.empty() ? "-" : os.display_version},
        {"Build", os.build_string},
        {"Architecture", os.architecture},
        {"Install Date", os.install_date},
        {"Last Boot", os.last_boot},
        {"Uptime", format_duration_sec(os.uptime_sec)},
        {"Locale", os.locale},
        {"Time Zone", os.timezone},
        {"Product ID", os.product_id},
        {"Registered Owner", os.registered_owner},
        {"Hotfixes Installed", std::to_string(os.hotfix_count)},
    }));

    out.push_back(sec("Computer", "computer", {
        {"Hostname", computer.hostname},
        {"User", computer.username},
        {"Manufacturer", computer.manufacturer},
        {"Model", [this] {
             std::string m = computer.manufacturer;
             if (!computer.model.empty()) {
                 if (!m.empty()) m += " ";
                 m += computer.model;
             }
             return m.empty() ? std::string("-") : m;
         }()},
        {"System Family", computer.system_family},
        {"Domain / Workgroup",
            !computer.domain.empty() ? computer.domain : computer.workgroup},
        {"Chassis", computer.chassis_type},
        {"System UUID", computer.uuid},
    }));

    out.push_back(sec("BIOS / Board", "bios", {
        {"BIOS Vendor", bios.vendor},
        {"BIOS Version", bios.version},
        {"BIOS Date", bios.date},
        {"Boot Mode", bios.mode},
        {"Secure Boot", bios.secure_boot},
        {"Board Vendor", bios.baseboard_manufacturer},
        {"Board Model", bios.baseboard_product},
        {"Board Version", bios.baseboard_version},
        {"Board Serial", bios.baseboard_serial},
    }));

    std::vector<KVRow> cpu_rows{
        {"Processor", cpu.brand},
        {"Vendor", cpu.vendor},
        {"Code Name", cpu.code_name},
        {"Socket", cpu.socket},
        {"Physical Cores", std::to_string(cpu.cores_physical)},
        {"Logical Cores", std::to_string(cpu.cores_logical)},
        {"Family / Model / Stepping", cpu.family_model_str},
        {"Base Clock", cpu.base_clock_mhz ? format_mhz(cpu.base_clock_mhz) : "-"},
        {"Current Clock", cpu.current_clock_mhz ? format_mhz(cpu.current_clock_mhz) : "-"},
        {"Max Clock", cpu.max_clock_mhz ? format_mhz(cpu.max_clock_mhz) : "-"},
        {"Load", cpu.load_pct},
        {"Temperature", cpu.temperature},
        {"Virtualization", cpu.hyper_visor},
    };
    for (const auto& c : cpu.caches)
        cpu_rows.push_back({c.level + " Cache",
                            std::to_string(c.size_kb) + " KB" +
                            (c.associativity.empty() ? "" : " (" + c.associativity + ")")});
    if (!cpu.features.empty()) {
        std::string feat;
        for (size_t i = 0; i < cpu.features.size(); ++i)
            feat += (i ? ", " : "") + cpu.features[i];
        cpu_rows.push_back({"Instructions", feat});
    }
    out.push_back(sec("CPU", "cpu", cpu_rows));

    std::vector<KVRow> mem_rows{
        {"Total Physical", format_bytes(memory.total_phys)},
        {"Available", format_bytes(memory.avail_phys)},
        {"Used", format_bytes(memory.total_phys - memory.avail_phys) +
                 " (" + std::to_string(unsigned(
                     pct_of(double(memory.total_phys - memory.avail_phys),
                            double(memory.total_phys)))) + " %)"},
        {"Virtual Total", format_bytes(memory.total_virtual)},
        {"Page File", format_bytes(memory.page_file_used) + " / " +
                      format_bytes(memory.page_file_total)},
        {"Slots Used", std::to_string(memory.slots_used) + " / " +
                       std::to_string(memory.slots_total)},
    };
    int idx = 1;
    for (const auto& m : memory.modules) {
        mem_rows.push_back({"Module " + m.slot,
            format_bytes(m.capacity_bytes) + " " + m.type + " @ " +
            std::to_string(m.speed_mtps) + " MT/s" +
            (m.manufacturer.empty() ? "" : " | " + m.manufacturer) +
            (m.part_number.empty() ? "" : " | " + m.part_number)});
        ++idx;
    }
    out.push_back(sec("Memory", "memory", mem_rows));

    for (const auto& g : gpus) {
        out.push_back(sec("GPU: " + g.name, "gpu", {
            {"Adapter", g.name},
            {"Vendor", g.vendor + " (" + g.vendor_id + ":" + g.device_id + ")"},
            {"VRAM", g.vram_bytes ? format_bytes(g.vram_bytes) : "-"},
            {"Shared Memory", g.shared_ram_bytes ? format_bytes(g.shared_ram_bytes) : "-"},
            {"Driver Version", g.driver_version},
            {"Driver Date", g.driver_date},
            {"Driver Model", g.driver_model},
            {"Video Mode", g.video_mode},
            {"Status", g.status},
        }));
    }

    for (const auto& d : disks) {
        out.push_back(sec("Disk #" + d.index + ": " + d.model, "disk", {
            {"Model", d.model},
            {"Serial", d.serial},
            {"Size", format_bytes(d.size_bytes)},
            {"Interface", d.iface},
            {"Media Type", d.media_type},
            {"Bus", d.bus_type},
            {"Firmware", d.firmware},
            {"Health", d.health},
            {"Temperature", d.temperature},
            {"TRIM", d.trim_supported},
            {"Partitions", std::to_string(d.partitions.size())},
        }));
    }

    std::vector<KVRow> vol_rows;
    for (const auto& v : volumes)
        vol_rows.push_back({v.letter + " " + (v.label.empty() ? "(Local Disk)" : v.label),
            format_bytes(v.free_bytes) + " free of " + format_bytes(v.total_bytes) +
            " [" + v.fs + "]" + (v.boot_volume ? " (Boot)" : "")});
    if (!vol_rows.empty())
        out.push_back(sec("Volumes", "disk", vol_rows));

    std::vector<KVRow> net_rows;
    for (const auto& a : adapters) {
        net_rows.push_back({a.name, a.description});
        net_rows.push_back({"  MAC", a.mac});
        net_rows.push_back({"  IPv4", a.ip4.empty() ? "-" : a.ip4});
        net_rows.push_back({"  IPv6", a.ip6.empty() ? "-" : a.ip6});
        net_rows.push_back({"  Gateway", a.gateway.empty() ? "-" : a.gateway});
        net_rows.push_back({"  DNS", a.dns_servers.empty() ? "-" : a.dns_servers});
        net_rows.push_back({"  Speed", a.link_speed_bps ? format_bps(a.link_speed_bps) : "-"});
        net_rows.push_back({"  Type / State", a.adapter_type + " / " + a.state});
        net_rows.push_back({"  Traffic (RX/TX)",
            format_bytes(a.rx_bytes) + " / " + format_bytes(a.tx_bytes)});
    }
    if (!net_rows.empty())
        out.push_back(sec("Network", "network", net_rows));

    for (const auto& m : monitors) {
        out.push_back(sec("Monitor: " + m.name, "monitor", {
            {"Display", m.name},
            {"Manufacturer", m.manufacturer},
            {"Serial", m.serial},
            {"Manufactured", m.edid_week_year},
            {"Native Resolution", m.native_resolution},
            {"Diagonal", m.diag_inches > 0 ?
                std::to_string(m.diag_inches).substr(0, 4) + "\"" : "-"},
            {"Current Mode", m.current_mode},
            {"Gamma", m.gamma},
            {"Primary", m.primary ? "Yes" : "No"},
        }));
    }

    if (!usb_devices.empty()) {
        std::vector<KVRow> urows;
        for (const auto& u : usb_devices)
            urows.push_back({u.name,
                u.vid_pid + (u.manufacturer.empty() ? "" : " | " + u.manufacturer) +
                (u.location.empty() ? "" : " | " + u.location)});
        out.push_back(sec("USB Devices (" + std::to_string(usb_devices.size()) + ")",
                          "usb", urows));
    }

    if (battery.present)
        out.push_back(sec("Battery", "battery", {
            {"Status", battery.state},
            {"Charge Level", battery.charge_pct},
            {"AC Line", battery.ac_line},
            {"Design Capacity", battery.design_capacity_wh},
            {"Full Charge Capacity", battery.full_charge_wh},
            {"Wear Level", battery.wear_pct},
            {"Voltage", battery.voltage_mv},
            {"Chemistry", battery.chemistry},
            {"Time Remaining", battery.remaining_time},
        }));

    if (!audio_devices.empty()) {
        std::vector<KVRow> arows;
        for (const auto& a : audio_devices)
            arows.push_back({a.endpoint, a.name + " [" + a.state + "]"});
        out.push_back(sec("Audio Devices", "audio", arows));
    }

    return out;
}

} // namespace krad

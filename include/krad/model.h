// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - device data model (platform-neutral, dependency-free)
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace krad {

constexpr const char* APP_ID = "krad.device.info";
constexpr const char* APP_NAME = "KradDeviceInfo";
constexpr const char* APP_VERSION = "1.1.0";

// ---------------------------------------------------------------- generic
struct KVRow {                       // one labeled row on a report page
    std::string key;
    std::string value;
};

struct ReportSection {               // a titled block of rows
    std::string title;
    std::string icon;                // symbolic name (cpu, memory...)
    std::vector<KVRow> rows;
};

struct TableItem {                   // entry for list-like pages (devices, apps)
    std::string name;
    std::string detail1;
    std::string detail2;
    std::string detail3;
};

// ---------------------------------------------------------------- system / OS
struct OsInfo {
    std::string product_name;        // e.g. Windows 11 Pro
    std::string display_version;     // 23H2
    std::string build_string;        // Build 22631.3155
    std::string version_major, version_minor, build_number;
    std::string edition, install_date, last_boot;
    std::string architecture;        // x64 / ARM64
    std::string locale, timezone;
    bool is_64bit = false;
    bool is_wow64 = false;
    std::uint32_t uptime_sec = 0;
    std::uint32_t process_count = 0, thread_count = 0, handle_count = 0;
    std::string product_id, registered_owner;
    std::uint32_t hotfix_count = 0;
};

struct ComputerInfo {
    std::string hostname, domain, workgroup;
    std::string manufacturer, model;             // computer system
    std::string system_family;
    std::string username;
    std::string uuid;                            // SMBIOS UUID
    std::string chassis_type;
};

// ---------------------------------------------------------------- bios
struct BiosInfo {
    std::string vendor, version, date;
    std::string mode;                            // UEFI / Legacy
    std::string secure_boot;                     // On/Off/Unsupported
    std::string baseboard_manufacturer, baseboard_product, baseboard_version;
    std::string baseboard_serial;
    std::vector<TableItem> smbios_extra;         // raw SMBIOS discoveries
};

// ---------------------------------------------------------------- cpu
struct CpuCache {
    std::string level;               // L1 / L2 / L3
    std::uint32_t size_kb = 0;
    std::string associativity;
};

struct CpuInfo {
    std::string vendor, brand;       // GenuineIntel / full marketing string
    std::string code_name;           // best effort from family/model
    std::string socket;
    std::uint32_t cores_physical = 0, cores_logical = 0;
    std::uint32_t family = 0, model = 0, stepping = 0;
    std::string family_model_str;
    double base_clock_mhz = 0.0;
    double current_clock_mhz = 0.0;
    double max_clock_mhz = 0.0;
    double voltage = 0.0;
    std::vector<CpuCache> caches;
    std::vector<std::string> features;           // SSE4.2 AVX2 ...
    std::string hyper_visor;                     // none / Hyper-V / KVM...
    std::string load_pct;                        // snapshot at collect time
    std::string temperature;                     // if ACPI provides
    std::vector<std::string> instruction_sets_extra;
};

// ---------------------------------------------------------------- memory
struct MemoryModule {
    std::string slot;                                // ChannelA-DIMM0
    std::uint64_t capacity_bytes = 0;
    std::uint32_t speed_mtps = 0;                    // configured MT/s
    std::uint32_t max_speed_mtps = 0;
    std::string type;                                // DDR4/DDR5...
    std::string form_factor;
    std::string manufacturer, part_number, serial;
    std::string ecc;
};

struct MemoryInfo {
    std::uint64_t total_phys = 0, avail_phys = 0;
    std::uint64_t total_virtual = 0, avail_virtual = 0;
    std::uint64_t page_file_total = 0, page_file_used = 0;
    std::uint32_t slots_used = 0, slots_total = 0;
    std::vector<MemoryModule> modules;
    std::uint32_t pagesize = 0;
};

// ---------------------------------------------------------------- gpu
struct GpuInfo {
    std::string name;
    std::string vendor;                              // NVIDIA/AMD/Intel
    std::string vendor_id, device_id;                // hex strings
    std::uint64_t vram_bytes = 0;
    std::uint64_t shared_ram_bytes = 0;
    std::string driver_version, driver_date;
    std::string driver_model;                        // WDDM 3.1 ...
    std::string dac_type;
    std::string video_mode;                          // current res@hz
    std::string status;                              // OK
    bool primary = false;
};

// ---------------------------------------------------------------- storage
struct VolumeInfo {
    std::string letter;                              // C:
    std::string label, fs;
    std::uint64_t total_bytes = 0, free_bytes = 0;
    std::string type;                                // Fixed/Removable/Network
    bool boot_volume = false;
};

struct PartitionInfo {
    std::string device_id;                           // Disk #0, Partition #1
    std::uint64_t size_bytes = 0;
    std::uint64_t offset_bytes = 0;
    std::string type_label;                          // GPT: Basic data ...
    bool bootable = false;
    std::string drive_letters;
};

struct DiskInfo {
    std::string index;                               // \\.\PHYSICALDRIVE0 idx
    std::string model;
    std::string serial;
    std::string iface;                           // NVMe/SATA/USB
    std::string media_type;                          // SSD/HDD/Unspecified
    std::string bus_type;
    std::uint64_t size_bytes = 0;
    std::uint32_t cylinders = 0, heads = 0, sectors_per_track = 0;
    std::string firmware;
    std::string health;                              // Healthy/Warning/...
    std::string temperature;
    std::string trim_supported;
    std::vector<PartitionInfo> partitions;
};

// ---------------------------------------------------------------- network
struct NetAdapter {
    std::string name;                                // friendly name
    std::string description;
    std::string mac;
    std::string ip4, ip6, gateway, dns_servers;
    std::string subnet_mask;
    std::uint64_t link_speed_bps = 0;                // bits/sec
    std::string adapter_type;                        // Ethernet/Wi-Fi/Loopback
    std::string state;                               // Up/Down/Disconnected
    std::uint64_t rx_bytes = 0, tx_bytes = 0;        // cumulative counters
    bool dhcp_enabled = false;
    std::string dhcp_server, lease_expires;
    int if_index = 0;
};

// ---------------------------------------------------------------- devices
struct MonitorInfo {
    std::string name;                                // EDID display product
    std::string manufacturer;                        // decoded PnP id
    std::string serial, edid_week_year;
    std::string native_resolution;
    std::string current_mode;                        // 1920x1080 @ 144Hz 32bit
    std::string connection;                          // internal/HDMI guess
    std::string gamma, dpi;
    bool primary = false;
    double diag_inches = 0.0;
};

struct UsbDevice {
    std::string name;
    std::string description;
    std::string manufacturer;
    std::string vid_pid;
    std::string location;
    std::string status;
    std::string class_guid_name;
};

struct BatteryInfo {
    bool present = false;
    std::string charge_pct;                          // "85 %"
    std::string ac_line;                             // Online/Offline
    std::string state;                               // Charging/Discharging...
    std::string voltage_mv;
    std::string chemistry;
    std::string design_capacity_wh, full_charge_wh;
    std::string wear_pct;
    std::string remaining_time;
    std::string saved_battery_time;
};

struct AudioDevice {
    std::string name;
    std::string endpoint;                            // Render/Capture
    std::string state;                               // Active/Disabled
    std::string default_flag;
};

// ---------------------------------------------------------------- software
struct InstalledApp {
    std::string name, version, publisher, install_date, size_str, location;
};

struct StartupEntry {
    std::string name, command, location;             // HKLM Run / Startup folder
    std::string state;
};

struct ServiceEntry {
    std::string name, display_name;
    std::string state;                               // Running/Stopped
    std::string start_mode;                          // Auto/Manual/Disabled
};

// ---------------------------------------------------------------- aggregate
struct DeviceReport {
    std::string generated_at;                        // ISO timestamp
    OsInfo os;
    ComputerInfo computer;
    BiosInfo bios;
    CpuInfo cpu;
    MemoryInfo memory;
    std::vector<GpuInfo> gpus;
    std::vector<DiskInfo> disks;
    std::vector<VolumeInfo> volumes;
    std::vector<NetAdapter> adapters;
    std::vector<MonitorInfo> monitors;
    std::vector<UsbDevice> usb_devices;
    BatteryInfo battery;
    std::vector<AudioDevice> audio_devices;
    std::vector<InstalledApp> installed_apps;
    std::vector<StartupEntry> startup_entries;
    std::vector<ServiceEntry> services;

    // flat sections mirror of the above for export + simple pages
    std::vector<ReportSection> sections() const;
};

// ---------------------------------------------------------------- perf
struct PerfSample {                  // one monitoring tick
    double cpu_total = 0.0;          // percent 0..100
    std::vector<double> cpu_cores;
    double ram_pct = 0.0;
    std::uint64_t ram_used = 0, ram_total = 0;
    double disk_active = 0.0;        // percent busy
    double disk_read_mbs = 0.0, disk_write_mbs = 0.0;
    double net_rx_kbps = 0.0, net_tx_kbps = 0.0;
    double gpu_pct = -1.0;           // negative = unavailable
    std::int64_t ts_ms = 0;          // epoch millis
};

} // namespace krad

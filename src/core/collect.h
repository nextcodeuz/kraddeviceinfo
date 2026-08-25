// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// Collector entry points - each implemented per platform.
// All functions must never throw; on failure they return empty/defaults.
#pragma once
#include <krad/model.h>

namespace krad {
namespace collect {

OsInfo        os_info();
ComputerInfo  computer_info();
BiosInfo      bios_info();
CpuInfo       cpu_info();
MemoryInfo    memory_info();
std::vector<GpuInfo>       gpus();
std::vector<DiskInfo>      disks();
std::vector<VolumeInfo>    volumes();
std::vector<NetAdapter>    network_adapters();
std::vector<MonitorInfo>   monitors();
std::vector<UsbDevice>     usb_devices();
BatteryInfo   battery();
std::vector<AudioDevice>   audio_devices();
std::vector<InstalledApp>  installed_apps();
std::vector<StartupEntry>  startup_entries();
std::vector<ServiceEntry>  services();

// full report helper (calls everything above)
DeviceReport  full_report();

// ---- realtime monitoring -------------------------------------------------
void perf_init();                       // prepare counters (PDH etc.)
void perf_shutdown();
PerfSample perf_sample();               // one tick, call from any thread

// true when running against real Windows APIs (false = demo/stub build)
bool native_backend();

// current CPU clock in MHz using performance counters; fallback = base_mhz
double perf_current_clock(double base_mhz);

} // namespace collect
} // namespace krad

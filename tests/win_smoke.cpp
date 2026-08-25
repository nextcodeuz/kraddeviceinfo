// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - windows core smoke test (mingw cross-compile target)
#ifdef _WIN32
#include <windows.h>
#endif
#include "core/collect.h"
#include "core/util.h"
#include <cstdio>

using namespace krad;

int main() {
    printf("== krad core smoke test (native=%d)\n", int(collect::native_backend()));
    OsInfo os = collect::os_info();
    printf("OS: %s %s (%s)\n", os.product_name.c_str(),
           os.build_string.c_str(), os.architecture.c_str());
    ComputerInfo c = collect::computer_info();
    printf("PC: %s %s | user=%s\n", c.manufacturer.c_str(), c.model.c_str(),
           c.username.c_str());
    BiosInfo b = collect::bios_info();
    printf("BIOS: %s %s mode=%s\n", b.vendor.c_str(), b.version.c_str(),
           b.mode.c_str());
    CpuInfo cpu = collect::cpu_info();
    printf("CPU: %s [%u/%u] base=%.0f max=%.0f\n", cpu.brand.c_str(),
           cpu.cores_physical, cpu.cores_logical, cpu.base_clock_mhz,
           cpu.max_clock_mhz);
    MemoryInfo m = collect::memory_info();
    printf("RAM: %s total, modules=%zu slots_used=%u\n",
           format_bytes(m.total_phys).c_str(), m.modules.size(), m.slots_used);
    auto gs = collect::gpus();
    for (auto& g : gs)
        printf("GPU: %s vram=%s drv=%s\n", g.name.c_str(),
               format_bytes(g.vram_bytes).c_str(), g.driver_version.c_str());
    auto ds = collect::disks();
    for (auto& d : ds)
        printf("Disk#%s: %s %s health=%s\n", d.index.c_str(), d.model.c_str(),
               format_bytes(d.size_bytes).c_str(), d.health.c_str());
    for (auto& v : collect::volumes())
        printf("Vol %s free=%s\n", v.letter.c_str(),
               format_bytes(v.free_bytes).c_str());
    for (auto& a : collect::network_adapters())
        printf("Net %s ip4=%s state=%s\n", a.name.c_str(), a.ip4.c_str(),
               a.state.c_str());
    for (auto& mo : collect::monitors())
        printf("Mon %s mode=%s\n", mo.name.c_str(), mo.current_mode.c_str());
    printf("USB devices: %zu\n", collect::usb_devices().size());
    printf("Apps: %zu  Startup: %zu  Services: %zu\n",
           collect::installed_apps().size(), collect::startup_entries().size(),
           collect::services().size());
    collect::perf_init();
    PerfSample s = collect::perf_sample();
    Sleep(300);
    s = collect::perf_sample();
    printf("Perf: cpu=%.1f%% ram=%.1f%% gpu=%.1f rx=%.1fKB/s\n", s.cpu_total,
           s.ram_pct, s.gpu_pct, s.net_rx_kbps);
    collect::perf_shutdown();
    printf("OK\n");
    return 0;
}

// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - aggregate report builder (parallel collectors)
#include "../../core/collect.h"
#include "wincompat.h"
#include <ctime>
#include <future>

namespace krad {
namespace collect {

DeviceReport full_report() {
    DeviceReport r;

    // Collectors are independent -> run them concurrently. Each worker
    // thread gets its own COM scope (WMI/MMDevice require per-thread COM).
    // Typical wall time drops from ~1.5-3 s serial to ~0.5-1 s.
    auto f_os   = std::async(std::launch::async, [] { win::ComScope c; return os_info(); });
    auto f_pc   = std::async(std::launch::async, [] { win::ComScope c; return computer_info(); });
    auto f_bios = std::async(std::launch::async, [] { win::ComScope c; return bios_info(); });
    auto f_cpu  = std::async(std::launch::async, [] { win::ComScope c; return cpu_info(); });
    auto f_mem  = std::async(std::launch::async, [] { win::ComScope c; return memory_info(); });
    auto f_gpu  = std::async(std::launch::async, [] { return gpus(); });
    auto f_dsk  = std::async(std::launch::async, [] { win::ComScope c; return disks(); });
    auto f_vol  = std::async(std::launch::async, [] { return volumes(); });
    auto f_net  = std::async(std::launch::async, [] { return network_adapters(); });
    auto f_mon  = std::async(std::launch::async, [] { return monitors(); });
    auto f_usb  = std::async(std::launch::async, [] { return usb_devices(); });
    auto f_bat  = std::async(std::launch::async, [] { win::ComScope c; return battery(); });
    auto f_aud  = std::async(std::launch::async, [] { win::ComScope c; return audio_devices(); });
    auto f_apps = std::async(std::launch::async, [] { return installed_apps(); });
    auto f_strt = std::async(std::launch::async, [] { return startup_entries(); });
    auto f_svc  = std::async(std::launch::async, [] { return services(); });

    r.os       = f_os.get();
    r.computer = f_pc.get();
    r.bios     = f_bios.get();
    r.cpu      = f_cpu.get();
    r.memory   = f_mem.get();
    r.gpus     = f_gpu.get();
    r.disks    = f_dsk.get();
    r.volumes  = f_vol.get();
    r.adapters = f_net.get();
    r.monitors = f_mon.get();
    r.usb_devices = f_usb.get();
    r.battery  = f_bat.get();
    r.audio_devices = f_aud.get();
    r.installed_apps  = f_apps.get();
    r.startup_entries = f_strt.get();
    r.services        = f_svc.get();

    char ts[32];
    std::uint64_t ms = win::now_epoch_ms();
    time_t t = time_t(ms / 1000);
    struct tm tmv;
    localtime_s(&tmv, &t);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    r.generated_at = ts;
    return r;
}

} // namespace collect
} // namespace krad

// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - realtime performance engine (win32)
// CPU via NtQuerySystemInformation, disks/GPU via PDH English counters,
// network via GetIfEntry(2) deltas. All lazily initialized, thread-safe.
#include "../../core/collect.h"
#include "wincompat.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <iphlpapi.h>
#include <psapi.h>
#pragma comment(lib, "iphlpapi.lib")

#include <mutex>

namespace krad {
namespace collect {

bool native_backend() { return true; }

// ---------------------------------------------------------------- cpu times
struct CpuTimes {
    std::uint64_t idle = 0, kernel = 0, user = 0;   // 100ns units
    bool valid = false;
};

static std::mutex g_cpu_mu;
static std::vector<CpuTimes> g_prev_core;
static CpuTimes g_prev_total;

static void filetime_u64(const FILETIME& ft, std::uint64_t& out) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    out = u.QuadPart;
}

static double delta_pct(const CpuTimes& prev, const CpuTimes& now) {
    if (!prev.valid || !now.valid) return 0.0;
    std::uint64_t total = (now.kernel + now.user) - (prev.kernel + prev.user);
    std::uint64_t busy  = total - (now.idle - prev.idle);
    if (!total) return 0.0;
    return pct_of(double(busy), double(total));
}

// SystemProcessorPerformanceInformation: per-core idle/kernel/user
struct SPPInfo {
    LARGE_INTEGER IdleTime, KernelTime, UserTime;
};

static bool sample_times(std::vector<CpuTimes>& cores, CpuTimes& total) {
    auto fn = win::nt_query_system_information();
    if (!fn) return false;

    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi, &fk, &fu)) return false;
    std::uint64_t i64, k64, u64v;
    filetime_u64(fi, i64); filetime_u64(fk, k64); filetime_u64(fu, u64v);
    total.idle = i64; total.kernel = k64; total.user = u64v;
    total.valid = true;

    // per-core
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    int n = int(si.dwNumberOfProcessors);
    cores.resize(size_t(n));
    std::vector<SPPInfo> spp(size_t(n) + 4);
    ULONG sz = sizeof(SPPInfo) * ULONG(spp.size());
    LONG r = fn(8 /*SystemProcessorPerformanceInformation*/, spp.data(), sz, nullptr);
    if (r == 0 && n > 0) {
        for (int c = 0; c < n; ++c) {
            CpuTimes t;
            t.idle   = std::uint64_t(spp[size_t(c)].IdleTime.QuadPart);
            t.kernel = std::uint64_t(spp[size_t(c)].KernelTime.QuadPart);
            t.user   = std::uint64_t(spp[size_t(c)].UserTime.QuadPart);
            t.valid  = size_t(c) < g_prev_core.size() &&
                       g_prev_core[size_t(c)].valid;
            cores[size_t(c)] = t;
        }
    } else {
        cores.clear();
    }
    return true;
}

double perf_current_clock(double base_mhz) {
    // % Processor Performance * base clock via PDH
    win::pdh::QueryImpl* q = win::pdh::open_query();
    if (!q) return base_mhz;
    void* h = nullptr;
    double pct = -1.0;
    if (win::pdh::add_english_counter(
            q, L"\\Processor Information(_Total)\\% Processor Performance", &h))
        win::pdh::fmt_double(h, pct);
    win::pdh::close_query(q);
    if (pct <= 0 || base_mhz <= 0) return base_mhz;
    return base_mhz * pct / 100.0;
}

// ---------------------------------------------------------------- pdh state
static std::mutex           g_pdh_mu;
static win::pdh::QueryImpl* g_pdh_q = nullptr;
static void* g_h_disk_active = nullptr;
static void* g_h_disk_read   = nullptr;
static void* g_h_disk_write  = nullptr;
static bool  g_pdh_tried     = false;

static void ensure_pdh() {
    if (g_pdh_tried) return;
    g_pdh_tried = true;
    if (!win::pdh::load()) return;
    g_pdh_q = win::pdh::open_query();
    if (!g_pdh_q) return;
    auto add = [&](void** h, const wchar_t* path) {
        win::pdh::add_english_counter(g_pdh_q, path, h);
    };
    add(&g_h_disk_active, L"\\PhysicalDisk(_Total)\\% Idle Time");
    add(&g_h_disk_read,   L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec");
    add(&g_h_disk_write,  L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec");
}

static void shutdown_pdh() {
    if (g_pdh_q) win::pdh::close_query(g_pdh_q);
    g_pdh_q = nullptr;
    g_h_disk_active = g_h_disk_read = g_h_disk_write = nullptr;
    g_pdh_tried = false;
}

// ---------------------------------------------------------------- network
static std::mutex g_net_mu;
static std::uint64_t g_prev_rx = 0, g_prev_tx = 0;
static std::int64_t  g_prev_net_ts = 0;

static void octets(std::uint64_t& rx, std::uint64_t& tx) {
    rx = tx = 0;
    PMIB_IFTABLE t1 = nullptr;
    ULONG sz = 0;
    if (GetIfTable(nullptr, &sz, FALSE) != ERROR_INSUFFICIENT_BUFFER) return;
    t1 = static_cast<PMIB_IFTABLE>(malloc(sz));
    if (t1 && GetIfTable(t1, &sz, FALSE) == NO_ERROR)
        for (UINT i = 0; i < t1->dwNumEntries; ++i) {
            auto& row = t1->table[i];
            if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            rx += row.dwInOctets;
            tx += row.dwOutOctets;
        }
    free(t1);
}

// ---------------------------------------------------------------- gpu
static std::mutex g_gpu_mu;
static std::map<std::wstring, void*> g_gpu_counters;
static win::pdh::QueryImpl* g_gpu_q = nullptr;
static bool g_gpu_tried = false;

static double gpu_utilization() {
    // GPU Engine(*)\Utilization Percentage summed over instances (Win10+).
    // Instance enumeration is expensive -> refresh only every 15 ticks;
    // counter handles are cached and the query is pumped every tick.
    std::lock_guard<std::mutex> lk(g_gpu_mu);
    if (!g_gpu_tried) {
        g_gpu_tried = true;
        if (win::pdh::load()) g_gpu_q = win::pdh::open_query();
    }
    if (!g_gpu_q) return -1.0;

    static int enum_cooldown = 0;
    static bool have_instances = false;
    if (!have_instances || enum_cooldown <= 0) {
        std::vector<std::wstring> instances;
        if (win::pdh::enum_object_instances(L"GPU Engine", instances)) {
            const wchar_t prefix[] = L"\\GPU Engine(";
            const wchar_t suffix[] = L")\\Utilization Percentage";
            for (auto& inst : instances) {
                if (inst.find(L"phys") != std::wstring::npos) continue;
                if (g_gpu_counters.count(inst)) continue;
                void* h = nullptr;
                if (win::pdh::add_localized_counter(g_gpu_q, 
                        (std::wstring(prefix) + inst + suffix).c_str(), &h) && h)
                    g_gpu_counters.emplace(inst, h);
            }
            have_instances = !g_gpu_counters.empty();
        }
        enum_cooldown = 15;                       // re-scan every 15 s
    } else {
        --enum_cooldown;
    }
    if (!have_instances) return -1.0;

    win::pdh::collect(g_gpu_q);                   // pump every tick
    double sum = 0.0;
    int counted = 0;
    for (auto& kv : g_gpu_counters) {
        double v = 0;
        if (win::pdh::fmt_double(kv.second, v) && v >= 0.0) {
            sum += v;
            ++counted;
        }
    }
    return counted ? sum : -1.0;
}

// ---------------------------------------------------------------- public API
void perf_init() {
    ensure_pdh();
}

void perf_shutdown() {
    shutdown_pdh();
}

PerfSample perf_sample() {
    PerfSample s;
    s.ts_ms = std::int64_t(win::now_epoch_ms());

    // cpu
    {
        std::lock_guard<std::mutex> lk(g_cpu_mu);
        std::vector<CpuTimes> cores;
        CpuTimes tot;
        if (sample_times(cores, tot)) {
            s.cpu_total = delta_pct(g_prev_total, tot);
            g_prev_total = tot;
            if (!cores.empty()) {
                s.cpu_cores.resize(cores.size());
                for (size_t i = 0; i < cores.size(); ++i) {
                    s.cpu_cores[i] =
                        delta_pct(i < g_prev_core.size() ? g_prev_core[i]
                                                         : CpuTimes{}, cores[i]);
                    g_prev_core.resize(cores.size());
                    g_prev_core[i] = cores[i];
                }
            }
        }
    }

    // memory
    MEMORYSTATUSEX ms; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        s.ram_used = ms.ullTotalPhys - ms.ullAvailPhys;
        s.ram_total = ms.ullTotalPhys;
        s.ram_pct = pct_of(double(s.ram_used), double(s.ram_total));
    }

    // disk
    ensure_pdh();
    {
        std::lock_guard<std::mutex> lk(g_pdh_mu);
        if (g_pdh_q) win::pdh::collect(g_pdh_q);   // pump rates every tick
        if (g_pdh_q && g_h_disk_active) {
            double idle = 0;
            if (win::pdh::fmt_double(g_h_disk_active, idle))
                s.disk_active = idle >= 0 ? 100.0 - idle : 0.0;
        }
        if (g_pdh_q && g_h_disk_read)
            win::pdh::fmt_double(g_h_disk_read, s.disk_read_mbs);
        if (g_pdh_q && g_h_disk_write)
            win::pdh::fmt_double(g_h_disk_write, s.disk_write_mbs);
        s.disk_read_mbs /= (1024.0 * 1024.0);
        s.disk_write_mbs /= (1024.0 * 1024.0);
    }

    // network
    {
        std::lock_guard<std::mutex> lk(g_net_mu);
        std::uint64_t rx = 0, tx = 0;
        octets(rx, tx);
        if (g_prev_net_ts && rx >= g_prev_rx) {
            double dt = double(s.ts_ms - g_prev_net_ts) / 1000.0;
            if (dt > 0.05) {
                s.net_rx_kbps = double(rx - g_prev_rx) / dt / 1024.0;
                s.net_tx_kbps = double(tx - g_prev_tx) / dt / 1024.0;
            }
        }
        g_prev_rx = rx; g_prev_tx = tx;
        g_prev_net_ts = s.ts_ms;
    }

    // gpu
    s.gpu_pct = gpu_utilization();

    return s;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

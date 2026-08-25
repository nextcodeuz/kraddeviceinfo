// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - non-Windows backend: reads real data from /proc & sysfs.
// Used for development/demo builds so the whole app is verifiable off-Windows.
// native_backend() returns false -> UI shows a "demo backend" badge.
#include "../core/collect.h"
#include "../core/util.h"

#ifndef _WIN32

#include <fstream>
#include <sstream>
#include <ctime>
#include <cctype>
#include <sys/utsname.h>
#include <map>
#include <set>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <dirent.h>

namespace krad {
namespace collect {

bool native_backend() { return false; }

static std::string read_os_pretty_name() {
    std::ifstream f("/etc/os-release");
    std::string l;
    while (std::getline(f, l)) {
        if (l.rfind("PRETTY_NAME=", 0) == 0) {
            std::string v = trim_copy(l.substr(12));
            if (!v.empty() && v.front() == '"' && v.back() == '"')
                v = v.substr(1, v.size() - 2);
            return v;
        }
    }
    return "Linux";
}

static std::string read_first_line(const char* path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return trim_copy(s);
}

static bool starts_with(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}

// ---------------------------------------------------------------- helpers
static void parse_cpuinfo(CpuInfo& c) {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    std::set<std::pair<int,int>> core_pairs;
    int pid = -1, cid = -1;
    while (std::getline(f, line)) {
        if (line.empty()) { pid = cid = -1; continue; }
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string key = trim_copy(line.substr(0, pos));
        std::string val = trim_copy(line.substr(pos + 1));
        if (key == "model name" && c.brand.empty()) c.brand = trim_copy(val);
        else if (key == "vendor_id" && c.vendor.empty()) c.vendor = val;
        else if (key == "cpu MHz") c.current_clock_mhz = atof(val.c_str());
        else if (key == "physical id") pid = atoi(val.c_str());
        else if (key == "core id") cid = atoi(val.c_str());
        else if (key == "flags" && c.features.empty()) {
            for (auto& fl : split_string(val, ' ')) {
                if (fl == "sse4_2") c.features.push_back("SSE4.2");
                else if (fl == "avx") c.features.push_back("AVX");
                else if (fl == "avx2") c.features.push_back("AVX2");
                else if (fl == "avx512f") c.features.push_back("AVX-512");
            }
        }
        if (pid >= 0 && cid >= 0) core_pairs.insert({pid, cid});
    }
    c.cores_logical = std::uint32_t(sysconf(_SC_NPROCESSORS_ONLN));
    c.cores_physical = core_pairs.size() ? std::uint32_t(core_pairs.size())
                                         : c.cores_logical;
}

OsInfo os_info() {
    OsInfo o;
    o.product_name = read_os_pretty_name();
    struct utsname un;
    if (!uname(&un)) {
        o.architecture = un.machine;
        o.build_string = std::string(un.release);
        o.version_major = un.release;
    }
    struct sysinfo si;
    if (!sysinfo(&si)) {
        o.uptime_sec = si.uptime;
        DIR* d = opendir("/proc");
        if (d) {
            while (dirent* e = readdir(d))
                if (std::isdigit((unsigned char)e->d_name[0])) ++o.process_count;
            closedir(d);
        }
        o.last_boot = epoch_to_str(std::uint64_t(time(nullptr)) - si.uptime);
    }
    char host[256] = "";
    gethostname(host, sizeof host);
    (void)host;
    return o;
}
ComputerInfo computer_info() { return {}; }
BiosInfo bios_info() { return {}; }
std::vector<GpuInfo> gpus() { return {}; }

CpuInfo cpu_info() {
    CpuInfo c;
    parse_cpuinfo(c);
    c.base_clock_mhz = c.current_clock_mhz;
    c.max_clock_mhz = c.current_clock_mhz;
    c.load_pct = "-";
    return c;
}

MemoryInfo memory_info() {
    MemoryInfo m;
    std::ifstream f("/proc/meminfo");
    std::string l;
    std::uint64_t total = 0, avail = 0;
    while (std::getline(f, l)) {
        if (starts_with(l, "MemTotal:"))
            total = strtoull(l.c_str() + 10, nullptr, 10) * 1024ULL;
        else if (starts_with(l, "MemAvailable:"))
            avail = strtoull(l.c_str() + 14, nullptr, 10) * 1024ULL;
    }
    m.total_phys = total;
    m.avail_phys = avail;
    m.total_virtual = total * 2;
    m.avail_virtual = avail;
    return m;
}

std::vector<DiskInfo> disks() {
    std::vector<DiskInfo> out;
    DiskInfo d;
    d.model = "Demo Virtual Disk";
    d.iface = "Virtual";
    d.health = "Healthy";
    d.media_type = "SSD";
    d.trim_supported = "Supported";
    std::ifstream f("/proc/partitions");
    std::string l;
    std::getline(f, l); std::getline(f, l);
    std::uint64_t max_kb = 0;
    while (std::getline(f, l)) {
        auto parts_ = split_string(trim_copy(l), ' ');
        std::vector<std::string> p;
        for (auto& x : parts_) if (!x.empty()) p.push_back(x);
        if (p.size() >= 4) {
            std::uint64_t kb = strtoull(p[2].c_str(), nullptr, 10);
            if (kb > max_kb) { max_kb = kb; d.index = p[3]; d.size_bytes = kb * 1024; }
        }
    }
    out.push_back(std::move(d));
    return out;
}

std::vector<VolumeInfo> volumes() {
    std::vector<VolumeInfo> out;
    VolumeInfo v;
    v.letter = "/"; v.fs = read_first_line("/proc/self/mounts").empty() ? "" : "ext4";
    v.type = "Fixed";
    struct statvfs st;
    if (!statvfs("/", &st)) {
        v.total_bytes = std::uint64_t(st.f_blocks) * st.f_frsize;
        v.free_bytes  = std::uint64_t(st.f_bavail) * st.f_frsize;
    }
    v.boot_volume = true;
    v.label = "(root)";
    out.push_back(v);
    return out;
}

std::vector<NetAdapter> network_adapters() {
    std::vector<NetAdapter> out;
    std::ifstream f("/proc/net/dev");
    std::string l;
    std::getline(f, l); std::getline(f, l);
    while (std::getline(f, l)) {
        auto pos = l.find(':');
        if (pos == std::string::npos) continue;
        NetAdapter a;
        a.name = trim_copy(l.substr(0, pos));
        std::vector<std::string> nums;
        for (auto& t : split_string(trim_copy(l.substr(pos + 1)), ' '))
            if (!t.empty()) nums.push_back(t);
        if (nums.size() >= 9) {
            a.rx_bytes = strtoull(nums[0].c_str(), nullptr, 10);
            a.tx_bytes = strtoull(nums[8].c_str(), nullptr, 10);
        }
        a.adapter_type = a.name == "lo" ? "Loopback" : "Ethernet";
        a.state = "Up";
        out.push_back(std::move(a));
    }
    return out;
}

std::vector<MonitorInfo> monitors() {
    MonitorInfo m;
    m.name = "Virtual Display";
    m.current_mode = getenv("KRAD_DEMO_RES") ? getenv("KRAD_DEMO_RES")
                                             : "1920x1080 @ 60 Hz, 24-bit";
    m.native_resolution = "1920x1080";
    m.primary = true;
    m.manufacturer = "DEM";
    m.gamma = "2.20";
    return {m};
}

std::vector<UsbDevice> usb_devices() { return {}; }

BatteryInfo battery() { return {}; }

std::vector<AudioDevice> audio_devices() {
    AudioDevice a;
    a.name = "Demo Audio Output";
    a.endpoint = "Output";
    a.state = "Active";
    return {a};
}

std::vector<InstalledApp> installed_apps() { return {}; }
std::vector<StartupEntry> startup_entries() { return {}; }
std::vector<ServiceEntry> services() { return {}; }

double perf_current_clock(double base_mhz) { return base_mhz; }
void perf_init() {}
void perf_shutdown() {}

PerfSample perf_sample() {
    static PerfSample prev;
    static std::uint64_t prev_idle_j = 0, prev_total_j = 0;
    static std::uint64_t prev_rx = 0, prev_tx = 0;
    static std::int64_t prev_ts = 0;

    PerfSample s;
    s.ts_ms = std::int64_t(time(nullptr)) * 1000;

    std::ifstream f("/proc/stat");
    std::string l;
    std::getline(f, l);
    if (starts_with(l, "cpu ")) {
        std::uint64_t j[5] = {}, sum = 0, idle = 0;
        int idx = 0;
        for (auto& t : split_string(trim_copy(l.substr(4)), ' ')) {
            if (t.empty()) continue;
            if (idx < 5) j[idx] = strtoull(t.c_str(), nullptr, 10);
            sum += j[idx];
            if (idx == 3 || idx == 4) idle += j[idx];
            ++idx;
        }
        if (prev_total_j) {
            double dt = double(sum - prev_total_j);
            s.cpu_total = dt > 0 ? pct_of(dt - double(idle - prev_idle_j), dt) : 0;
        }
        prev_total_j = sum; prev_idle_j = idle;
    }
    // per-core (demo: mirror total)
    s.cpu_cores.assign(size_t(sysconf(_SC_NPROCESSORS_ONLN)), s.cpu_total);

    MemoryInfo mi = memory_info();
    s.ram_used = mi.total_phys - mi.avail_phys;
    s.ram_total = mi.total_phys;
    s.ram_pct = pct_of(double(s.ram_used), double(mi.total_phys));

    std::ifstream nf("/proc/net/dev");
    std::getline(nf, l); std::getline(nf, l);
    std::uint64_t rx = 0, tx = 0;
    while (std::getline(nf, l)) {
        auto pos = l.find(':');
        if (pos == std::string::npos) continue;
        if (trim_copy(l.substr(0, pos)) == "lo") continue;
        std::vector<std::string> nums;
        for (auto& t : split_string(trim_copy(l.substr(pos + 1)), ' '))
            if (!t.empty()) nums.push_back(t);
        if (nums.size() >= 9) {
            rx += strtoull(nums[0].c_str(), nullptr, 10);
            tx += strtoull(nums[8].c_str(), nullptr, 10);
        }
    }
    if (prev_ts) {
        double dt = double(s.ts_ms - prev_ts) / 1000.0;
        if (dt > 0.05) {
            s.net_rx_kbps = rx > prev_rx ? double(rx - prev_rx) / dt / 1024.0 : 0;
            s.net_tx_kbps = tx > prev_tx ? double(tx - prev_tx) / dt / 1024.0 : 0;
        }
    }
    prev_rx = rx; prev_tx = tx; prev_ts = s.ts_ms;
    (void)prev;
    return s;
}

DeviceReport full_report() {
    DeviceReport r;
    r.os = os_info();
    r.computer.hostname = "demo-host";
    r.cpu = cpu_info();
    r.memory = memory_info();
    r.disks = disks();
    r.volumes = volumes();
    r.adapters = network_adapters();
    r.monitors = monitors();
    r.audio_devices = audio_devices();
    time_t t = time(nullptr);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    r.generated_at = ts;
    return r;
}

} // namespace collect
} // namespace krad

#endif // !_WIN32

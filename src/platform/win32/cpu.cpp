// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - CPU collector (win32): cpuid + topology + clocks
#include "../../core/collect.h"
#include "wincompat.h"
#include "wmi_helper.h"
#include "smbios.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <map>

namespace krad {
namespace collect {

using win::reg_read_u32;

#if defined(__GNUC__)
static void do_cpuid(std::uint32_t leaf, std::uint32_t subleaf,
                     std::uint32_t out[4]) {
    __asm__ __volatile__("cpuid"
                         : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
                         : "a"(leaf), "c"(subleaf));
}
#else
#include <intrin.h>
static void do_cpuid(std::uint32_t leaf, std::uint32_t subleaf,
                     std::uint32_t out[4]) {
    int o[4];
    __cpuidex(o, int(leaf), int(subleaf));
    out[0] = std::uint32_t(o[0]); out[1] = std::uint32_t(o[1]);
    out[2] = std::uint32_t(o[2]); out[3] = std::uint32_t(o[3]);
}
#endif

static std::string cpu_vendor_string() {
    std::uint32_t r[4];
    do_cpuid(0, 0, r);
    char v[13] = {0};
    memcpy(v,     &r[1], 4);
    memcpy(v + 4, &r[3], 4);
    memcpy(v + 8, &r[2], 4);
    return v;
}

static std::string brand_string() {
    std::uint32_t r[4];
    do_cpuid(0x80000000u, 0, r);
    if (r[0] < 0x80000004u) return {};
    char b[49] = {0};
    for (int i = 0; i < 3; ++i) {
        do_cpuid(0x80000002u + i, 0, r);
        memcpy(b + i * 16, r, 16);
    }
    // collapse double spaces
    std::string s;
    for (char c : b) {
        if (c == ' ' && !s.empty() && s.back() == ' ') continue;
        s += c;
    }
    return trim_copy(s);
}

static std::string guess_code_name(std::uint32_t family,
                                   std::uint32_t model,
                                   const std::string& brand) {
    std::string b = lower_copy(brand);

    if (contains_ci(brand, "AMD")) {
        if (contains_ci(b, "ryzen"))    return "Zen microarchitecture";
        if (family == 0xF)              return "Bulldozer/Star class";
    }
    if (contains_ci(brand, "Intel") || contains_ci(b, "genuine")) {
        struct KM { unsigned fam; unsigned lo, hi; const char* name; };
        static const KM tbl[] = {
            {6, 0x9E, 0x9E, "Coffee Lake"}, {6, 0xA5, 0xA5, "Comet Lake"},
            {6, 0xB7, 0xB7, "Raptor Lake"}, {6, 0xBA, 0xBA, "Alder Lake-P"},
            {6, 0x97, 0x97, "Alder Lake"},  {6, 0xBE, 0xBF, "Alder Lake-N"},
            {6, 0xAA, 0xAA, "Arrow Lake"},  {6, 0x7E, 0x7E, "Ice Lake-U"},
            {6, 0x8C, 0x8C, "Tiger Lake"},  {6, 0xA7, 0xA7, "Rocket Lake"},
            {6, 0x8D, 0x8D, "Ice Lake-Server"},
        };
        for (auto& e : tbl)
            if (e.fam == family && model >= e.lo && model <= e.hi) return e.name;
        if (family == 6 && model >= 0x80) return "Modern Intel Core";
        if (family == 15) return "Netburst";
    }
    return {};
}

static void collect_features(CpuInfo& c) {
    auto add = [&](const char* f) { c.features.push_back(f); };
    std::uint32_t r1[4], r7[4] = {}, re[4];

    do_cpuid(1, 0, r1);
    if (r1[3] & (1 << 23)) add("MMX");
    if (r1[3] & (1 << 25)) add("SSE");
    if (r1[3] & (1 << 26)) add("SSE2");
    if (r1[2] & (1 <<  0)) add("SSE3");
    if (r1[2] & (1 <<  9)) add("SSSE3");
    if (r1[2] & (1 << 19)) add("SSE4.1");
    if (r1[2] & (1 << 20)) add("SSE4.2");
    if (r1[2] & (1 << 25)) add("AES-NI");
    const bool avx = (r1[2] & (1 << 27)) && (r1[2] & (1 << 28));

    do_cpuid(7, 0, r7);   // safe even when unsupported (returns zeros)
    if (avx) add("AVX");
    if (avx && (r7[1] & (1 << 5)))  add("AVX2");
    if (r7[1] & (1 << 3))           add("BMI1");
    if (r7[1] & (1 << 8))           add("BMI2");
    if (r7[1] & (1 << 29))          add("SHA");

    // AVX-512 requires OSXSAVE + XCR0 ymm/zmm state
    if ((r7[1] & (1 << 16)) && avx) {
        std::uint64_t xcr0 = 0;
#if defined(__GNUC__)
        std::uint32_t lo = 0, hi = 0;
        __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        xcr0 = (std::uint64_t(hi) << 32) | lo;
#else
        xcr0 = _xgetbv(0);
#endif
        if ((xcr0 & 0xE6ULL) == 0xE6ULL)
            add("AVX-512");
    }

    do_cpuid(0x80000001u, 0, re);
    if (re[3] & (1 << 29)) add("x86-64-v2+");
    if (re[2] & (1 << 5))  add("RDTSCP");
    if (r1[2] & (1 << 30)) add("Hypervisor");
}

static const char* detect_hypervisor() {
    std::uint32_t r1[4];
    do_cpuid(1, 0, r1);
    if (!(r1[2] & (1u << 31))) return "None (bare metal)";
    char sig[13] = {0};
    std::uint32_t r[4];
    do_cpuid(0x40000000u, 0, r);
    memcpy(sig,     &r[1], 4);
    memcpy(sig + 4, &r[2], 4);
    memcpy(sig + 8, &r[3], 4);
    std::string s(sig);
    if (s == "Microsoft Hv") return "Hyper-V";
    if (s == "KVMKVMKVM")    return "KVM";
    if (s == "VMwareVMware") return "VMware";
    if (s == "XenVMMXenVMM") return "Xen";
    if (s == "prl hyperv")   return "Parallels";
    if (s == "TCGTCGTCGTCG") return "QEMU (TCG)";
    if (s == "VBoxVBoxVBox") return "VirtualBox";
    return s.c_str();
}


CpuInfo cpu_info() {
    CpuInfo c;

    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    c.cores_logical = si.dwNumberOfProcessors;

    std::uint32_t r[4];
    do_cpuid(1, 0, r);
    c.family   = (r[0] >> 8) & 0xF;
    c.model    = (r[0] >> 4) & 0xF;
    c.stepping = r[0] & 0xF;
    std::uint32_t ext_family = (r[0] >> 20) & 0xFF;
    std::uint32_t ext_model  = (r[0] >> 16) & 0xF;
    if (c.family == 0xF) c.family += ext_family;
    if (c.family == 0x6 || c.family >= 0xF) c.model += ext_model << 4;

    char fm[64];
    snprintf(fm, sizeof fm, "%u / %Xh / %u", c.family, c.model, c.stepping);
    c.family_model_str = fm;

    c.vendor = cpu_vendor_string();
    c.brand  = brand_string();
    collect_features(c);
    c.hyper_visor = detect_hypervisor();

    // topology: physical cores via GetLogicalProcessorInformationEx (Win7+)
    typedef BOOL (WINAPI *GLPIEX_t)(LOGICAL_PROCESSOR_RELATIONSHIP,
                                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
    static GLPIEX_t glpiex = reinterpret_cast<GLPIEX_t>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"),
                       "GetLogicalProcessorInformationEx"));
    DWORD len = 0;
    if (glpiex && !glpiex(RelationProcessorCore, nullptr, &len) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && len) {
        std::vector<char> buf(len);
        if (glpiex(RelationProcessorCore,
                   reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                       buf.data()), &len)) {
            BYTE* p = reinterpret_cast<BYTE*>(buf.data());
            BYTE* e = p + len;
            while (p < e) {
                auto* inf = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(p);
                if (inf->Relationship == RelationProcessorCore) ++c.cores_physical;
                p += inf->Size;
            }
        }
    }
    if (!c.cores_physical) {
        // XP-era fallback
        DWORD n = 0;
        GetLogicalProcessorInformation(nullptr, &n);
        if (n) {
            std::vector<char> buf(n);
            if (GetLogicalProcessorInformation(
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>(buf.data()), &n))
                for (DWORD off = 0; off + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= n;
                     off += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)) {
                    auto* inf = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(
                        buf.data() + off);
                    if (inf->Relationship == RelationProcessorCore) ++c.cores_physical;
                }
        }
    }
    if (!c.cores_physical) c.cores_physical = c.cores_logical;   // last resort

    // caches from GetLogicalProcessorInformationEx(RelationCache)
    std::map<std::string, std::uint32_t> cache_sizes;             // level -> max KB
    if (glpiex && !glpiex(RelationCache, nullptr, &len) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && len) {
        std::vector<char> buf(len);
        if (glpiex(RelationCache,
                   reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                       buf.data()), &len)) {
            BYTE* p = reinterpret_cast<BYTE*>(buf.data());
            BYTE* e = p + len;
            while (p < e) {
                auto* inf = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(p);
                if (inf->Relationship == RelationCache &&
                    inf->Cache.Level >= 1 && inf->Cache.Level <= 3) {
                    char key[8]; snprintf(key, sizeof key, "L%u", inf->Cache.Level);
                    std::uint32_t kb = inf->Cache.CacheSize / 1024;
                    auto& cur = cache_sizes[key];
                    if (kb > cur) cur = kb;
                    if (inf->Cache.Associativity == 0xFF) ;
                }
                p += inf->Size;
            }
        }
    }
    for (auto& kv : cache_sizes) {
        CpuCache cc; cc.level = kv.first; cc.size_kb = kv.second;
        c.caches.push_back(cc);
    }

    // clocks: base from registry, current via PDH perf, max via WMI/SMBIOS
    std::uint32_t base_mhz = 0;
    if (reg_read_u32(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", base_mhz))
        c.base_clock_mhz = base_mhz;
    wmi::Session s;
    if (s.connect()) {
        std::vector<std::string> row;
        if (s.query_one(L"SELECT MaxClockSpeed, LoadPercentage, CurrentVoltage "
                        L"FROM Win32_Processor",
                        {L"MaxClockSpeed", L"LoadPercentage", L"CurrentVoltage"}, row)) {
            c.max_clock_mhz = atof(row[0].c_str());
            if (!row[1].empty()) c.load_pct = row[1] + " %";
            if (!row[2].empty()) {
                int mv10 = atoi(row[2].c_str());      // encodings vary wildly
                if (mv10 > 0 && mv10 < 1000)
                    c.voltage = (mv10 & 0x80) ? (mv10 & 0x7F) / 10.0 : mv10 * 0.1 * 1.6;
            }
        }
    }
    smbios_enrich_cpu(c);
    c.code_name = guess_code_name(c.family, c.model, c.brand);
    if (!c.max_clock_mhz && c.base_clock_mhz) c.max_clock_mhz = c.base_clock_mhz;
    if (!c.current_clock_mhz) c.current_clock_mhz =
        perf_current_clock(c.base_clock_mhz);

    // temperature: ACPI thermal zone (often unsupported - ignore failure)
    if (s.connected()) {
        std::vector<std::vector<std::string>> rows;
        if (s.query(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature "
                    L"WHERE Active=TRUE", {L"CurrentTemperature"}, rows) &&
            !rows.empty() && !rows[0][0].empty()) {
            double tenths_kelvin = atof(rows[0][0].c_str());
            if (tenths_kelvin > 1000) {
                char t[16];
                snprintf(t, sizeof t, "%.1f °C", tenths_kelvin / 10.0 - 273.15);
                c.temperature = t;
            }
        }
    }
    return c;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

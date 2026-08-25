// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

#include "util.h"
#include <cstdio>
#include <cctype>
#include <ctime>
#include <algorithm>

namespace krad {

static void fmt_bytes(char* buf, size_t n, double v, const char* unit) {
    std::snprintf(buf, n, "%.2f %s", v, unit);
}

std::string format_bytes(std::uint64_t v) {
    char buf[64];
    const double kib = 1024.0;
    if (v >= std::uint64_t(kib * kib * kib * kib)) fmt_bytes(buf, sizeof buf, v / (kib*kib*kib*kib), "TB");
    else if (v >= kib*kib*kib)                     fmt_bytes(buf, sizeof buf, v / (kib*kib*kib), "GB");
    else if (v >= kib*kib)                         fmt_bytes(buf, sizeof buf, v / (kib*kib), "MB");
    else if (v >= kib)                             fmt_bytes(buf, sizeof buf, v / kib, "KB");
    else                                           std::snprintf(buf, sizeof buf, "%u B", unsigned(v));
    return buf;
}

std::string format_bytes_speed(std::uint64_t bps) {
    return format_bytes(bps) + "/s";
}

std::string format_mhz(double mhz) {
    char buf[64];
    if (mhz >= 1000.0) std::snprintf(buf, sizeof buf, "%.2f GHz", mhz / 1000.0);
    else               std::snprintf(buf, sizeof buf, "%.0f MHz", mhz);
    return buf;
}

std::string format_duration_sec(std::uint64_t s) {
    char buf[96];
    std::uint64_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (d > 0) std::snprintf(buf, sizeof buf, "%ud %uh %um %us", unsigned(d), unsigned(h), unsigned(m), unsigned(sec));
    else if (h > 0) std::snprintf(buf, sizeof buf, "%uh %um %us", unsigned(h), unsigned(m), unsigned(sec));
    else std::snprintf(buf, sizeof buf, "%um %us", unsigned(m), unsigned(sec));
    return buf;
}

std::string format_bps(std::uint64_t bps) {
    char buf[64];
    double g = bps / 1e9, m = bps / 1e6, k = bps / 1e3;
    if (g >= 1.0)      std::snprintf(buf, sizeof buf, "%.1f Gbps", g);
    else if (m >= 1.0) std::snprintf(buf, sizeof buf, "%.1f Mbps", m);
    else if (k >= 1.0) std::snprintf(buf, sizeof buf, "%.0f Kbps", k);
    else               std::snprintf(buf, sizeof buf, "%u bps", unsigned(bps));
    return buf;
}

std::string trim_copy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower_copy(const std::string& s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    return r;
}

std::string upper_copy(const std::string& s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return char(std::toupper(c)); });
    return r;
}

std::vector<std::string> split_string(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return lower_copy(hay).find(lower_copy(needle)) != std::string::npos;
}

bool starts_with_ci(const std::string& hay, const std::string& prefix) {
    if (prefix.size() > hay.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), hay.begin(),
                      [](char a, char b) {
                          return std::tolower((unsigned char)a) ==
                                 std::tolower((unsigned char)b);
                      });
}

std::string hex32(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%04X", v);
    return buf;
}

double pct_of(double part, double total) {
    if (total <= 0.0) return 0.0;
    double r = part / total * 100.0;
    if (r < 0)   r = 0;
    if (r > 100) r = 100;
    return r;
}

std::string epoch_to_str(std::uint64_t unix_sec) {
    if (!unix_sec) return "-";
    time_t t = time_t(unix_sec);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

} // namespace krad

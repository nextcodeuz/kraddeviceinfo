// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// string/format utilities shared by core + app layers (dependency-free)
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace krad {

std::string format_bytes(std::uint64_t v);          // 1.50 GB
std::string format_bytes_speed(std::uint64_t bytes_per_sec);
std::string format_mhz(double mhz);                 // 3.60 GHz / 800 MHz
std::string format_duration_sec(std::uint64_t s);   // 2d 4h 12m 33s
std::string format_bps(std::uint64_t bits_per_sec); // 1 Gbps
std::string trim_copy(const std::string& s);
std::string lower_copy(const std::string& s);
std::string upper_copy(const std::string& s);
std::vector<std::string> split_string(const std::string& s, char sep);
bool contains_ci(const std::string& hay, const std::string& needle);
bool starts_with_ci(const std::string& hay, const std::string& prefix);
std::string hex32(std::uint32_t v);                 // 0x8086
double pct_of(double part, double total);           // clamped 0..100
std::string epoch_to_str(std::uint64_t unix_sec);   // "%Y-%m-%d %H:%M:%S"

} // namespace krad

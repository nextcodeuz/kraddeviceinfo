// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - exporters: JSON / HTML / TXT / CSV (Qt-free)
#pragma once

#include <krad/model.h>
#include <string>
#include <vector>

namespace krad {
namespace export_ {

struct PerfSeriesPoint {           // optional monitoring history for reports
    std::int64_t ts = 0;
    double cpu = 0, ram = 0, gpu = -1;
};

bool write_json(const DeviceReport& r, const std::string& path);
bool write_html(const DeviceReport& r, const std::string& path,
                const std::vector<PerfSeriesPoint>& history = {});
bool write_txt (const DeviceReport& r, const std::string& path);
bool write_csv (const DeviceReport& r, const std::string& path);

// string variants (used by GUI preview)
std::string to_json(const DeviceReport& r);
std::string to_html(const DeviceReport& r,
                    const std::vector<PerfSeriesPoint>& history = {});
std::string to_txt (const DeviceReport& r);
std::string to_csv (const DeviceReport& r);

} // namespace export_
} // namespace krad

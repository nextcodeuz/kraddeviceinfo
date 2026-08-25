// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - command-line mode: export / benchmark / monitor loop
#pragma once

#include <string>
#include <vector>

namespace krad {
namespace cli {

// returns process exit code; 0x7FFFFFFF signals "launch GUI instead"
int run(const std::vector<std::string>& args);

void print_usage();

} // namespace cli
} // namespace krad

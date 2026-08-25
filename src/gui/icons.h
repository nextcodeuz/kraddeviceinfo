// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - runtime painted icons
#pragma once

#include <QIcon>

namespace krad {
namespace ui {

enum class Icon {
    Overview = 0, Cpu, Memory, Gpu, Disk, Network,
    Globe, Monitor, Software, Bench, Report
};

struct Icons {
    static QIcon get(Icon id);
};

QIcon makeAppIcon();

} // namespace ui
} // namespace krad

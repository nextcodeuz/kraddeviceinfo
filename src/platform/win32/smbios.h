// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - smbios enrichment hooks (win-only)
#pragma once

#ifdef _WIN32

#include <krad/model.h>

namespace krad {
namespace collect {

std::string smbios_version();
void        smbios_enrich_bios(BiosInfo& b);
void        smbios_enrich_cpu(CpuInfo& c);
void        smbios_enrich_memory(MemoryInfo& m);

} // namespace collect
} // namespace krad

#endif // _WIN32

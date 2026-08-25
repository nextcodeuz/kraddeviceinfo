// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - storage collectors (win32): disks, partitions, volumes
#include "../../core/collect.h"
#include "wincompat.h"
#include "wmi_helper.h"
#include "../../core/util.h"

#ifdef _WIN32

namespace krad {
namespace collect {

using win::wide_to_utf8;

static const char* bus_type_name(int bt) {
    switch (bt) {
    case 1: return "SCSI";  case 2: return "ATAPI"; case 3: return "ATA";
    case 4: return "IEEE1394"; case 5: return "SSA"; case 6: return "Fibre Channel";
    case 7: return "USB";   case 8: return "RAID";   case 9: return "iSCSI";
    case 10: return "SAS";  case 11: return "SATA";  case 12: return "SD";
    case 13: return "MMC";  case 14: return "Virtual";
    case 15: return "File-Backed Virtual"; case 16: return "Storage Spaces";
    case 17: return "NVMe";
    default: return "";
    }
}
static const char* media_type_name(int mt) {
    switch (mt) {
    case 3:  return "SSD";
    case 4:  return "HDD";
    case 5:  return "Removable";
    default: return "";
    }
}

std::vector<DiskInfo> disks() {
    std::vector<DiskInfo> out;
    wmi::Session s;

    const std::vector<std::wstring> dd_props{
        L"Index", L"Model", L"SerialNumber", L"InterfaceType", L"Size",
        L"FirmwareRevision", L"TotalCylinders", L"TotalHeads",
        L"SectorsPerTrack", L"Partitions"};
    if (s.connect()) {
        std::vector<std::vector<std::string>> rows;
        s.query(L"SELECT Index, Model, SerialNumber, InterfaceType, Size, "
                L"FirmwareRevision, TotalCylinders, TotalHeads, SectorsPerTrack, "
                L"Partitions FROM Win32_DiskDrive", dd_props, rows);
        for (auto& r : rows) {
            DiskInfo d;
            d.index      = r[0];
            d.model      = trim_copy(r[1]);
            d.serial     = trim_copy(r[2]);
            d.iface  = r[3];
            d.size_bytes = strtoull(r[4].c_str(), nullptr, 10);
            d.firmware   = trim_copy(r[5]);
            d.cylinders  = std::uint32_t(atol(r[6].c_str()));
            d.heads      = std::uint32_t(atol(r[7].c_str()));
            d.sectors_per_track = std::uint32_t(atol(r[8].c_str()));
            out.push_back(std::move(d));
        }
    }

    // enrich with MSFT_PhysicalDisk (Win8+): media/bus/health/firmware
    wmi::Session st;
    if (st.connect(L"ROOT\\Microsoft\\Windows\\Storage")) {
        std::vector<std::vector<std::string>> rows;
        st.query(L"SELECT FriendlyName, SerialNumber, MediaType, BusType, "
                 L"HealthStatus, FirmwareVersion FROM MSFT_PhysicalDisk",
                 {L"FriendlyName", L"SerialNumber", L"MediaType", L"BusType",
                  L"HealthStatus", L"FirmwareVersion"}, rows);
        for (auto& r : rows) {
            for (auto& d : out) {
                bool match = !r[1].empty() && r[1] == d.serial;
                if (!match && !r[0].empty())
                    match = contains_ci(d.model, r[0]);
                if (!match) continue;
                int mt = atoi(r[2].c_str());
                if (const char* n = media_type_name(mt)) d.media_type = n;
                int bt = atoi(r[3].c_str());
                if (const char* n = bus_type_name(bt)) {
                    d.bus_type = n;
                    if (!d.iface.empty() &&
                        lower_copy(d.iface) == "scsi" && bt == 17)
                        d.iface = "NVMe";
                }
                int hs = atoi(r[4].c_str());
                d.health = hs == 0 ? "Healthy" :
                           hs == 1 ? "Warning" : hs == 2 ? "Unhealthy" : "Unknown";
                if (!d.firmware.empty()) d.firmware = r[5];
                break;
            }
        }
    }
    for (auto& d : out) {
        if (d.health.empty()) d.health = "Unknown";
        if (d.bus_type.empty()) d.bus_type = d.iface;
        bool ssd_like = contains_ci(d.model, "SSD") ||
                        contains_ci(d.model, "NVMe") ||
                        contains_ci(d.iface, "NVMe");
        if (d.media_type.empty()) d.media_type = ssd_like ? "SSD (inferred)" : "";
        d.trim_supported =
            ssd_like ? "Supported" : (contains_ci(lower_copy(d.media_type), "ssd")
                                        ? "Supported" : "-");
        // partitions
        if (s.connected()) {
            wchar_t q[256];
            swprintf(q, 256,
                L"ASSOCIATORS OF {Win32_DiskDrive.DeviceID=\"\\\\\\\\.\\\\"
                L"PHYSICALDRIVE%s\"} WHERE AssocClass=Win32_DiskDriveToDiskPartition",
                d.index.c_str());
            std::vector<std::vector<std::string>> parts;
            s.query(q, {L"DeviceID", L"Size", L"StartingOffset", L"BootPartition",
                        L"Type"}, parts);
            for (auto& pr : parts) {
                PartitionInfo pi;
                pi.device_id     = pr[0];
                pi.size_bytes    = strtoull(pr[1].c_str(), nullptr, 10);
                pi.offset_bytes  = strtoull(pr[2].c_str(), nullptr, 10);
                pi.bootable      = pr[3] != "0" && !pr[3].empty();
                pi.type_label    = pr[4];
                // drive letters mapped to this partition
                wchar_t q2[512];
                swprintf(q2, 512,
                    L"ASSOCIATORS OF {Win32_DiskPartition.DeviceID=\"%s\"} "
                    L"WHERE AssocClass=Win32_LogicalDiskToPartition",
                    win::utf8_to_wide(pr[0]).c_str());
                std::vector<std::vector<std::string>> lds;
                s.query(q2, {L"DeviceID"}, lds);
                for (auto& ld : lds)
                    pi.drive_letters += (pi.drive_letters.empty() ? "" : ",") + ld[0];
                d.partitions.push_back(std::move(pi));
            }
        }
    }
    return out;
}

std::vector<VolumeInfo> volumes() {
    std::vector<VolumeInfo> out;

    wchar_t drives[512];
    DWORD n = GetLogicalDriveStringsW(511, drives);
    if (!n || n > 511) return out;

    std::string sysroot(3, 0);
    {
        wchar_t sp[MAX_PATH];
        if (GetSystemDirectoryW(sp, MAX_PATH)) {
            sysroot[0] = char(tolower(char(sp[0])));
            sysroot[1] = ':'; sysroot[2] = ' ';
        }
    }

    for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
        VolumeInfo v;
        v.letter = wide_to_utf8(std::wstring(p, wcslen(p) > 3 ? 3 : wcslen(p)));
        UINT dt = GetDriveTypeW(p);
        switch (dt) {
        case DRIVE_FIXED:     v.type = "Fixed"; break;
        case DRIVE_REMOVABLE: v.type = "Removable"; break;
        case DRIVE_CDROM:     v.type = "CD/DVD"; break;
        case DRIVE_REMOTE:    v.type = "Network"; break;
        case DRIVE_RAMDISK:   v.type = "RAM disk"; break;
        case DRIVE_NO_ROOT_DIR: v.type = "No root"; break;
        default:              v.type = "Unknown";
        }

        wchar_t label[MAX_PATH + 1] = L"", fs[64] = L"";
        DWORD flags = 0;
        if (GetVolumeInformationW(p, label, MAX_PATH, nullptr, nullptr,
                                  &flags, fs, 63)) {
            v.label = win::wide_to_utf8(label);
            v.fs    = win::wide_to_utf8(fs);
        } else if (dt == DRIVE_NO_ROOT_DIR || dt == 0) {
            continue;                                   // skip dead entries
        }

        ULARGE_INTEGER total = {}, freeb = {};
        if (GetDiskFreeSpaceExW(p, nullptr, &total, &freeb)) {
            v.total_bytes = total.QuadPart;
            v.free_bytes  = freeb.QuadPart;
        }
        v.boot_volume = sysroot.size() == 3 &&
                        tolower(v.letter[0]) == sysroot[0];
        out.push_back(std::move(v));
    }
    return out;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

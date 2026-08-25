// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - benchmark module (dependency-free, runs in worker threads)
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>

namespace krad {
namespace bench {

struct Progress {                       // emitted during a run
    std::string stage;                  // "Single thread", "4K random write"...
    double      fraction = 0.0;         // 0..1 within the whole suite
    double      live_score = 0.0;       // stage-specific live metric
};

using CancelFn  = std::function<bool()>;
using ProgressFn= std::function<void(const Progress&)>;

struct CpuResult {
    double single_score = 0;            // higher is better (ops/s normalized)
    double multi_score = 0;
    int    threads = 0;
};
struct MemResult {
    double copy_gbs = 0, read_gbs = 0, write_gbs = 0;
    double latency_ns = 0;
};
struct DiskItem {
    std::string label;                  // "Sequential read" ...
    double mbps = 0;
    double iops = 0;
};
struct DiskResult {
    std::string target;                 // drive/folder used
    std::uint64_t bytes_written = 0;    // cleaned up afterwards
    std::vector<DiskItem> items;
};

CpuResult   run_cpu(int seconds, const ProgressFn& progress,
                    const CancelFn& cancel);
MemResult   run_mem(int seconds, const ProgressFn& progress,
                    const CancelFn& cancel);
DiskResult  run_disk(const std::string& dir, int seconds,
                     const ProgressFn& progress, const CancelFn& cancel);

} // namespace bench
} // namespace krad

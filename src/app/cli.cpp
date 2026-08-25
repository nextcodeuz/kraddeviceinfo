// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - CLI implementation
#include "cli.h"
#include "export.h"
#include "../core/collect.h"
#include "../core/bench.h"
#include "../core/util.h"
#include <krad/model.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace krad {
namespace cli {

#ifdef _WIN32
// GUI-subsystem exe still prints when launched from a console.
static void attach_console() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$",  "r", stdin);
    }
}
static void detach_console() {
    if (GetConsoleWindow()) {
        fflush(stdout); fflush(stderr);
        FreeConsole();
    }
}
#else
static void attach_console() {}
static void detach_console() {}
#endif

constexpr int EXIT_GUI = 0x7FFFFFFF;

void print_usage() {
    printf(
        "%s v%s (%s)\n\n"
        "Usage:\n"
        "  kraddeviceinfo [options]\n\n"
        "Export options:\n"
        "  --export json|html|txt|csv   export full report to file\n"
        "  --output FILE               output path (default: report.<fmt>)\n\n"
        "Benchmark options:\n"
        "  --bench cpu[,memory][,disk] run benchmarks (default: cpu)\n"
        "  --duration SECONDS          per-suite duration (default 10)\n"
        "  --drive DIR                 target folder for disk bench\n\n"
        "Monitor:\n"
        "  --monitor                   live stats to console (Ctrl+C stops)\n"
        "  --interval MS               refresh interval (default 1000)\n\n"
        "Other:\n"
        "  --help                      this help\n"
        "  --version                   version string\n",
        krad::APP_NAME, krad::APP_VERSION, krad::APP_ID);
}

static std::string default_output_name(const std::string& fmt) {
    return "kraddeviceinfo-report." + fmt;
}

static int cmd_export(const std::string& fmt, const std::string& out) {
    DeviceReport r = collect::full_report();
    std::string path = out.empty() ? default_output_name(fmt) : out;
    bool ok = false;
    if      (fmt == "json") ok = export_::write_json(r, path);
    else if (fmt == "html") ok = export_::write_html(r, path);
    else if (fmt == "txt" || fmt == "text") ok = export_::write_txt(r, path);
    else if (fmt == "csv")  ok = export_::write_csv(r, path);
    else { fprintf(stderr, "Unknown format '%s'\n", fmt.c_str()); return 2; }

    if (!ok) {
        fprintf(stderr, "Cannot write '%s'\n", path.c_str());
        return 3;
    }
    printf("Report written to %s\n", path.c_str());
    return 0;
}

static void bench_progress_line(const char* stage, double frac, double live) {
    printf("\r%-24s %5.1f%%  live=%-10.1f", stage, frac * 100.0, live);
    fflush(stdout);
}

static int cmd_benchmark(const std::string& what, int seconds,
                         const std::string& drive) {
    auto parts = split_string(what, ',');
    if (parts.empty() || what == "all")
        parts = {"cpu", "memory", "disk"};

    bool cancelled = false;
    for (auto& p : parts) {
        std::string item = lower_copy(trim_copy(p));
        if (item.empty()) continue;

        if (item[0] == 'c') {
            printf("CPU benchmark (%d threads)...\n", int(std::thread::hardware_concurrency()));
            auto res = bench::run_cpu(seconds, [](const bench::Progress& pr)
                { bench_progress_line(pr.stage.c_str(), pr.fraction, pr.live_score); },
                [&] { return false; });
            printf("\rCPU score: single=%.1f  multi=%.1f MT/s-equivalent "
                   "(%dx speedup %.2f)\n\n",
                   res.single_score, res.multi_score, res.threads,
                   res.single_score ? res.multi_score / res.single_score : 0.0);
        } else if (item[0] == 'm') {
            printf("Memory benchmark...\n");
            auto res = bench::run_mem(seconds, [](const bench::Progress& pr)
                { bench_progress_line(pr.stage.c_str(), pr.fraction, pr.live_score); },
                [&] { return false; });
            printf("\rMemory: copy=%.1f GB/s  read=%.1f GB/s  write=%.1f GB/s"
                   "  latency=%.0f ns\n\n",
                   res.copy_gbs, res.read_gbs, res.write_gbs, res.latency_ns);
        } else if (item[0] == 'd') {
            printf("Disk benchmark on %s ...\n",
                   drive.empty() ? "current directory" : drive.c_str());
            auto res = bench::run_disk(drive, seconds, [](const bench::Progress& pr)
                { bench_progress_line(pr.stage.c_str(), pr.fraction, pr.live_score); },
                [&] { return false; });
            for (auto& it : res.items)
                printf("  %-20s %8.1f MB/s %10.0f IOPS\n",
                       it.label.c_str(), it.mbps, it.iops);
            printf("\n");
        }
        (void)cancelled;
    }
    return 0;
}

static int cmd_monitor(int interval_ms) {
    collect::perf_init();
    printf("%-8s %-7s %-6s %-9s %-9s %-9s %-9s\n",
           "CPU%", "RAM%", "GPU%", "DiskR MB/s", "DiskW MB/s",
           "NetRX KB/s", "NetTX KB/s");
    while (true) {
        PerfSample s = collect::perf_sample();
        printf("%-8.1f %-7.1f %-6.1f %-11.1f %-11.1f %-11.1f %-9.1f\r",
               s.cpu_total, s.ram_pct,
               s.gpu_pct >= 0 ? s.gpu_pct : 0.0,
               s.disk_read_mbs, s.disk_write_mbs,
               s.net_rx_kbps, s.net_tx_kbps);
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    return 0;
}

int run(const std::vector<std::string>& args) {
    attach_console();

    std::string export_fmt, output, bench_what, drive;
    int seconds = 10, interval_ms = 1000;
    bool do_bench = false, do_monitor = false, want_help = false,
         want_version = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](std::string& dst) -> bool {
            if (i + 1 < args.size()) { dst = args[++i]; return true; }
            return false;
        };
        if      (a == "--export" || a == "-e")  next(export_fmt);
        else if (a == "--output" || a == "-o")  next(output);
        else if (a == "--bench")  { next(bench_what); if (bench_what.empty()) bench_what="cpu"; do_bench = true; }
        else if (a == "--duration" || a == "-d") {
            std::string v;
            if (next(v)) seconds = atoi(v.c_str());
        }
        else if (a == "--drive")                next(drive);
        else if (a == "--monitor")              do_monitor = true;
        else if (a == "--interval") { std::string v; next(v); interval_ms = atoi(v.c_str()); }
        else if (a == "--help" || a == "-h")    want_help = true;
        else if (a == "--version" || a == "-V") want_version = true;
        else {
            fprintf(stderr, "Unknown option: %s\n", a.c_str());
            detach_console();
            return 1;
        }
    }

    int code = EXIT_GUI;
    if (want_help)       { print_usage(); code = 0; }
    else if (want_version){ printf("%s v%s\n", krad::APP_NAME, krad::APP_VERSION); code = 0; }
    else if (!export_fmt.empty())     code = cmd_export(export_fmt, output);
    else if (do_bench)                code = cmd_benchmark(bench_what, seconds, drive);
    else if (do_monitor)              code = cmd_monitor(interval_ms);

    if (code != EXIT_GUI) detach_console();
    return code;
}

} // namespace cli
} // namespace krad

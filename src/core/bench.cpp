// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - benchmark implementation
// CPU: mixed integer workload (PRNG + multiply-add + popcount), scaled by
//      wall-clock. MEM: memcpy bandwidth + pointer-chase latency.
// DISK: sequential write/read + 4K random write/read on a temp file.
#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace krad {
namespace bench {

namespace {

using Clock = std::chrono::steady_clock;

static double elapsed_s(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static unsigned hardware_threads() {
    unsigned n = std::thread::hardware_concurrency();
    return n ? n : 1u;
}

#if defined(_MSC_VER)
static int popcnt64(std::uint64_t v) { return __popcnt64(v); }
#else
static int popcnt64(std::uint64_t v) { return __builtin_popcountll(v); }
#endif

// ---- CPU worker: deterministic integer mix, returns ops completed --------
struct CpuWork {
    std::uint64_t ops = 0;
    std::uint64_t seed;
};

static void cpu_worker(CpuWork& w, const std::atomic<bool>& stop,
                       const std::atomic<double>& deadline_frac) {
    (void)deadline_frac;
    std::uint64_t x = w.seed, acc = 0, iters = 0;
    const std::uint64_t mask = 0x9E3779B97F4A7C15ULL;
    while (!stop.load(std::memory_order_relaxed)) {
        // 64 iterations per loop to amortize the stop check
        for (int i = 0; i < 64; ++i) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            acc += x * mask;
            acc ^= popcnt64(x) * 0xFF51AFD7ED558CCDULL;
        }
        iters += 64;
        if ((iters & 0xFFFFF) == 0) {          // ~every 1M iterations
            if (acc == 0xdeadbeefdeadbeefULL) w.ops = 0;   // keep compiler honest
        }
    }
    w.ops = iters + (acc == 1);                // fold acc so it is not dead
}

} // namespace

CpuResult run_cpu(int seconds, const ProgressFn& progress,
                  const CancelFn& cancel) {
    CpuResult r;
    r.threads = int(hardware_threads());

    const double single_secs = std::max(2.0, seconds * 0.35);
    const double multi_secs  = std::max(3.0, seconds * 0.65);
    const double total = single_secs + multi_secs;

    auto emit = [&](const char* stage, double done, double live) {
        if (progress) {
            Progress pr;
            pr.stage = stage;
            pr.fraction = done / total;
            pr.live_score = live;
            progress(pr);
        }
    };

    // ---------------- single thread ----------------
    {
        std::atomic<bool> stop{false};
        CpuWork w{0, 0x123456789ABCDEFULL};
        auto t0 = Clock::now();
        std::thread th([&] { cpu_worker(w, stop, std::atomic<double>{0}); });
        while (elapsed_s(t0) < single_secs) {
            if (cancel && cancel()) break;
            double e = elapsed_s(t0);
            emit("Single thread", e, w.ops / std::max(0.25, e));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        stop = true;
        th.join();
        double e = elapsed_s(t0);
        r.single_score = w.ops / e / 1e6;          // M-iterations/s
        emit("Single thread done", single_secs, r.single_score);
    }

    // ---------------- multi thread ----------------
    {
        std::atomic<bool> stop{false};
        std::vector<CpuWork> works(size_t(r.threads));
        for (size_t i = 0; i < works.size(); ++i)
            works[i].seed = 0xDEADBEEF0000ULL + i * 7919ULL;
        std::vector<std::unique_ptr<std::thread>> ths;
        for (auto& w : works)
            ths.push_back(std::make_unique<std::thread>(
                [&] { cpu_worker(w, stop, std::atomic<double>{0}); }));
        auto t0 = Clock::now();
        while (elapsed_s(t0) < multi_secs) {
            if (cancel && cancel()) break;
            std::uint64_t sum = 0;
            for (auto& w : works) sum += w.ops;
            double e = elapsed_s(t0);
            emit("All threads", single_secs + e,
                 double(sum) / std::max(0.25, e) / 1e6);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        stop = true;
        for (auto& th : ths) th->join();
        std::uint64_t sum = 0;
        for (auto& w : works) sum += w.ops;
        r.multi_score = double(sum) / elapsed_s(t0) / 1e6;
        emit("Done", total, r.multi_score);
    }
    return r;
}

MemResult run_mem(int seconds, const ProgressFn& progress,
                  const CancelFn& cancel) {
    MemResult r;
    const size_t buf_size = size_t(128) << 20;   // 128 MB
    const size_t lat_size = size_t(32)  << 20;   // 32 MB pointer-chase region

    std::vector<char> a(buf_size), b(buf_size);
    for (size_t i = 0; i < buf_size; i += 4096) b[i] = char(i);

    auto emit = [&](const char* stage, double f, double live) {
        if (progress) {
            Progress pr; pr.stage = stage; pr.fraction = f; pr.live_score = live;
            progress(pr);
        }
    };

    const int passes_total = 6;
    int pass = 0;

    // copy bandwidth
    {
        auto t0 = Clock::now();
        int copies = 0;
        while (elapsed_s(t0) < double(seconds) / passes_total &&
               !(cancel && cancel())) {
            memcpy(a.data(), b.data(), buf_size);
            ++copies;
        }
        double s = elapsed_s(t0);
        r.copy_gbs = copies ? (double(copies) * buf_size) / s / 1e9 : 0;
        emit("Memory copy", double(++pass) / passes_total, r.copy_gbs);
    }
    // write bandwidth
    {
        auto t0 = Clock::now();
        int n = 0;
        while (elapsed_s(t0) < double(seconds) / passes_total &&
               !(cancel && cancel())) {
            memset(a.data(), int(n & 0xFF), buf_size);
            ++n;
        }
        double s = elapsed_s(t0);
        r.write_gbs = n ? (double(n) * buf_size) / s / 1e9 : 0;
        emit("Memory write", double(++pass) / passes_total, r.write_gbs);
    }
    // read bandwidth
    {
        volatile std::uint64_t sink = 0;
        auto t0 = Clock::now();
        int n = 0;
        while (elapsed_s(t0) < double(seconds) / passes_total &&
               !(cancel && cancel())) {
            const std::uint64_t* p =
                reinterpret_cast<const std::uint64_t*>(b.data());
            size_t cnt = buf_size / sizeof(std::uint64_t);
            std::uint64_t local = 0;
            for (size_t i = 0; i < cnt; i += 8) local += p[i];
            sink += local;
            ++n;
        }
        double s = elapsed_s(t0);
        (void)sink;
        r.read_gbs = n ? (double(n) * buf_size) / s / 1e9 : 0;
        emit("Memory read", double(++pass) / passes_total, r.read_gbs);
    }
    // latency via pointer chase
    {
        size_t n = lat_size / sizeof(std::uint32_t);
        std::vector<std::uint32_t> next(n);
        for (size_t i = 0; i < n; ++i) next[std::uint32_t(i)] = std::uint32_t(i);
        std::mt19937 rng(42);
        for (size_t i = n - 1; i > 0; --i)
            std::swap(next[i], next[rng() % (i + 1)]);

        const int chases = 12;
        long long ns_total = 0;
        for (int c = 0; c < chases && !(cancel && cancel()); ++c) {
            std::uint32_t idx = 0;
            auto t0 = Clock::now();
            for (size_t step = 0; step < n; ++step)
                idx = next[idx];
            double chase_ns = elapsed_s(t0) * 1e9 / double(n);
            ns_total += (long long)chase_ns;
            (void)idx;
            emit("Memory latency",
                 (pass + double(c + 1) / chases) / passes_total, chase_ns);
        }
        r.latency_ns = chases ? double(ns_total) / chases : 0;
        pass += 3;
        emit("Benchmark complete", 1.0, r.latency_ns);
    }
    return r;
}

DiskResult run_disk(const std::string& dir, int seconds,
                    const ProgressFn& progress, const CancelFn& cancel) {
    DiskResult r;
    r.target = dir.empty() ? "." : dir;
    std::string path = r.target;
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += "/";
#ifdef _WIN32
    path += "krad_diskbench.tmp";
#else
    path += "krad_diskbench.tmp";
#endif

    FILE* f = nullptr;
#if defined(_MSC_VER)
    fopen_s(&f, path.c_str(), "w+b");
#else
    f = fopen(path.c_str(), "w+b");
#endif
    if (!f) {
        r.target += " (access denied)";
        return r;
    }

    auto emit = [&](const char* stage, double fr, double live) {
        if (progress) {
            Progress pr; pr.stage = stage; pr.fraction = fr; pr.live_score = live;
            progress(pr);
        }
    };

    const size_t seq_block = size_t(4)   << 20;   // 4 MB sequential blocks
    const size_t rnd_block = size_t(4)          ; // 4 KiB random blocks
    const double stage_secs = std::max(2.0, seconds / 4.0);
    std::vector<char> block(seq_block);
    std::mt19937_64 rng(7);

    auto seek_random = [&](std::FILE* file, size_t filesize) {
        size_t off = size_t(rng() % (filesize / rnd_block)) * rnd_block;
#if defined(_WIN32)
        _fseeki64(file, (long long)off, SEEK_SET);
#else
        fseeko(file, off, SEEK_SET);
#endif
    };
    auto file_size_now = [](std::FILE* file) -> size_t {
#if defined(_WIN32)
        long long cur = (long long)_ftelli64(file);
        _fseeki64(file, 0, SEEK_END);
        long long sz = (long long)_ftelli64(file);
        _fseeki64(file, cur, SEEK_SET);
        return size_t(sz > 0 ? sz : 0);
#else
        long cur = ftell(file);
        fseek(file, 0, SEEK_END);
        long sz = ftell(file);
        fseek(file, cur, SEEK_SET);
        return size_t(sz > 0 ? sz : 0);
#endif
    };

    // preallocate 256 MB by writing seq blocks once
    size_t prealloc = size_t(256) << 20;
    size_t written_pre = 0;
    while (written_pre < prealloc) {
        fwrite(block.data(), 1, seq_block, f);
        written_pre += seq_block;
    }
    fflush(f);

    // ---- sequential write ----
    {
        size_t total_file = file_size_now(f);
        auto t0 = Clock::now();
        std::uint64_t bytes = 0;
        while (elapsed_s(t0) < stage_secs && !(cancel && cancel())) {
            fwrite(block.data(), 1, seq_block, f);
            bytes += seq_block;
            total_file += seq_block;
            emit("Sequential write",
                 (bytes ? double(bytes % (1ull<<30)) : 0.0) / 4.0,
                 bytes / std::max(0.25, elapsed_s(t0)) / 1e6);
        }
        fflush(f);
        double s = elapsed_s(t0);
        if (s > 0) {
            DiskItem it;
            it.label = "Sequential write";
            it.mbps  = bytes / s / 1e6;
            r.items.push_back(it);
        }
    }

    // ---- sequential read ----
    {
        auto t0 = Clock::now();
        std::uint64_t bytes = 0;
        rewind(f);
        while (elapsed_s(t0) < stage_secs && !(cancel && cancel())) {
            size_t got = fread(block.data(), 1, seq_block, f);
            if (got == 0) { rewind(f); continue; }
            bytes += got;
            emit("Sequential read", 0.5, bytes / std::max(0.25, elapsed_s(t0)) / 1e6);
        }
        double s = elapsed_s(t0);
        if (s > 0) {
            DiskItem it;
            it.label = "Sequential read";
            it.mbps  = bytes / s / 1e6;
            r.items.push_back(it);
        }
    }

    size_t filesize = file_size_now(f);

    // ---- random 4K write ----
    {
        auto t0 = Clock::now();
        std::uint64_t ops = 0;
        while (elapsed_s(t0) < stage_secs && !(cancel && cancel())) {
            seek_random(f, filesize);
            fwrite(block.data(), 1, rnd_block, f);
            ++ops;
            if ((ops & 0xFF) == 0) fflush(f);
            emit("4K random write", 0.75, double(ops) / std::max(0.25, elapsed_s(t0)));
        }
        fflush(f);
        double s = elapsed_s(t0);
        if (s > 0) {
            DiskItem it;
            it.label = "4K random write";
            it.mbps  = ops * rnd_block / s / 1e6;
            it.iops  = ops / s;
            r.items.push_back(it);
        }
    }

    // ---- random 4K read ----
    {
        auto t0 = Clock::now();
        std::uint64_t ops = 0;
        while (elapsed_s(t0) < stage_secs && !(cancel && cancel())) {
            seek_random(f, filesize);
            fread(block.data(), 1, rnd_block, f);
            ++ops;
            emit("4K random read", 1.0, double(ops) / std::max(0.25, elapsed_s(t0)));
        }
        double s = elapsed_s(t0);
        if (s > 0) {
            DiskItem it;
            it.label = "4K random read";
            it.mbps  = ops * rnd_block / s / 1e6;
            it.iops  = ops / s;
            r.items.push_back(it);
        }
    }

    fclose(f);
    remove(path.c_str());
    r.bytes_written = prealloc;
    return r;
}

} // namespace bench
} // namespace krad

// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - PDH wrapper: dynamically loaded, English counters preferred
#include "wincompat.h"

#ifdef _WIN32

#include <pdh.h>

#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA ((LONG)0x800007DA)
#endif

namespace krad {
namespace win {
namespace pdh {

struct QueryImpl { PDH_HQUERY q = nullptr; };

typedef LONG (WINAPI *PdhOpenQueryW_t)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
typedef LONG (WINAPI *PdhCloseQuery_t)(PDH_HQUERY);
typedef LONG (WINAPI *PdhAddEnglishCounterW_t)(PDH_HQUERY, LPCWSTR, DWORD_PTR,
                                               PDH_HCOUNTER*);
typedef LONG (WINAPI *PdhAddCounterW_t)(PDH_HQUERY, LPCWSTR, DWORD_PTR,
                                        PDH_HCOUNTER*);
typedef LONG (WINAPI *PdhCollectQueryData_t)(PDH_HQUERY);
typedef LONG (WINAPI *PdhGetFormattedCounterValue_t)(PDH_HCOUNTER, DWORD,
                                                     LPDWORD, PPDH_FMT_COUNTERVALUE);
typedef LONG (WINAPI *PdhEnumObjectItemsW_t)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD,
                                             LPWSTR, LPDWORD, LPWSTR, LPDWORD,
                                             DWORD);

static PdhOpenQueryW_t              f_open;
static PdhCloseQuery_t              f_close;
static PdhAddEnglishCounterW_t      f_add_en;
static PdhAddCounterW_t             f_add_loc;
static PdhCollectQueryData_t        f_collect;
static PdhGetFormattedCounterValue_t f_fmt;
static PdhEnumObjectItemsW_t        f_enum;

bool load() {
    static bool tried = false;
    if (tried) return f_open != nullptr;
    tried = true;
    HMODULE m = LoadLibraryA("pdh.dll");
    if (!m) return false;
    auto get = [&](const char* n) { return GetProcAddress(m, n); };
    f_open    = reinterpret_cast<PdhOpenQueryW_t>(get("PdhOpenQueryW"));
    f_close   = reinterpret_cast<PdhCloseQuery_t>(get("PdhCloseQuery"));
    f_add_en  = reinterpret_cast<PdhAddEnglishCounterW_t>(get("PdhAddEnglishCounterW"));
    f_add_loc = reinterpret_cast<PdhAddCounterW_t>(get("PdhAddCounterW"));
    f_collect = reinterpret_cast<PdhCollectQueryData_t>(get("PdhCollectQueryData"));
    f_fmt     = reinterpret_cast<PdhGetFormattedCounterValue_t>(get("PdhGetFormattedCounterValue"));
    f_enum    = reinterpret_cast<PdhEnumObjectItemsW_t>(get("PdhEnumObjectItemsW"));
    return f_open && f_close && f_add_en && f_collect && f_fmt;
}

void unload() {}

QueryImpl* open_query() {
    if (!load()) return nullptr;
    auto* impl_ = new QueryImpl();
    if (f_open(nullptr, 0, &impl_->q) != ERROR_SUCCESS || !impl_->q) {
        delete impl_;
        return nullptr;
    }
    return impl_;
}

void close_query(QueryImpl*& q) {
    if (q) {
        if (q->q) f_close(q->q);
        delete q;
        q = nullptr;
    }
}

bool collect(QueryImpl* q) {
    if (q && q->q) { f_collect(q->q); return true; }   // pump: rates need this every tick
    return false;
}

bool add_english_counter(QueryImpl* q, const wchar_t* path, void** handle) {
    if (!q || !q->q || !f_add_en) return false;
    PDH_HCOUNTER h = nullptr;
    if (f_add_en(q->q, path, 0, &h) != ERROR_SUCCESS) return false;
    collect(q);                        // prime
    if (handle) *handle = static_cast<void*>(h);
    return true;
}

bool add_localized_counter(QueryImpl* q, const wchar_t* path, void** handle) {
    if (!q || !q->q || !f_add_loc) return false;
    PDH_HCOUNTER h = nullptr;
    if (f_add_loc(q->q, path, 0, &h) != ERROR_SUCCESS) return false;
    if (handle) *handle = static_cast<void*>(h);
    return true;
}

bool fmt_double(void* handle, double& out) {
    if (!handle || !f_fmt) return false;
    PDH_FMT_COUNTERVALUE v;
    if (f_fmt(reinterpret_cast<PDH_HCOUNTER>(handle), PDH_FMT_DOUBLE,
              nullptr, &v) != ERROR_SUCCESS)
        return false;
    if (v.CStatus != ERROR_SUCCESS) return false;
    out = v.doubleValue;
    return true;
}

bool enum_object_instances(const wchar_t* obj, std::vector<std::wstring>& out) {
    if (!f_enum) return false;
    // list detail PERF_DETAIL_WIZARD covers all; size query then fetch
    DWORD sz_list = 0, sz_inst = 0;
    LONG r = f_enum(nullptr, nullptr, obj, PERF_DETAIL_WIZARD, nullptr,
                    &sz_list, nullptr, &sz_inst, 0);
    if (r != PDH_MORE_DATA || sz_inst < 2) return false;
    std::vector<wchar_t> inst_buf(sz_inst + 2, 0);
    std::vector<wchar_t> counter_buf(sz_list ? sz_list + 2 : 64, 0);
    r = f_enum(nullptr, nullptr, obj, PERF_DETAIL_WIZARD,
               counter_buf.data(), &sz_list, inst_buf.data(), &sz_inst, 0);
    if (r != ERROR_SUCCESS) return false;
    for (wchar_t* p = inst_buf.data(); *p; p += wcslen(p) + 1)
        out.push_back(p);
    return !out.empty();
}

} // namespace pdh
} // namespace win
} // namespace krad

#endif // _WIN32

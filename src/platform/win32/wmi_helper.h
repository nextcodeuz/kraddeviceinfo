// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - WMI wrapper (win-only header)
#pragma once

#ifdef _WIN32

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace krad { namespace win { class ComScope; } }

namespace krad {
namespace wmi {

// RAII WMI session. Requires CoInitializeEx on the calling thread.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool connect(const wchar_t* ns = L"ROOT\\CIMV2");
    bool connected() const { return service_ != nullptr; }

    // props: WMI property names (ASCII); rows: UTF-8 values aligned to props
    bool query(const std::wstring& wql,
               const std::vector<std::wstring>& props,
               std::vector<std::vector<std::string>>& rows);
    bool query_one(const std::wstring& wql,
                   const std::vector<std::wstring>& props,
                   std::vector<std::string>& row);

private:
    std::unique_ptr<win::ComScope> com_;
    void* locator_ = nullptr;
    void* service_ = nullptr;
};

std::string   variant_to_utf8(void* variant);
std::uint64_t variant_to_u64(void* variant);

} // namespace wmi
} // namespace krad

#endif // _WIN32

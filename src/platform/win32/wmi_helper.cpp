// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - WMI wrapper implementation
#include "wmi_helper.h"
#include "wincompat.h"

#ifdef _WIN32

#define _WIN32_DCOM
#include <objbase.h>
#include <wbemidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// MinGW lacks the _bstr_t helpers from comutil in some configs - use raw BSTRs.
static BSTR bstr_from(const wchar_t* s) { return SysAllocString(s); }

namespace krad {
namespace wmi {

std::string variant_to_utf8(void* pv) {
    if (!pv) return {};
    VARIANT* v = static_cast<VARIANT*>(pv);
    switch (v->vt) {
    case VT_BSTR:  return win::wide_to_utf8(V_BSTR(v));
    case VT_BOOL:  return V_BOOL(v) ? "True" : "False";
    case VT_I4: case VT_UI4: case VT_I2: case VT_UI2:
    case VT_I8: case VT_UI8: case VT_INT: case VT_UINT: {
        char buf[32];
        unsigned long long u = 0;
        switch (v->vt) {
        case VT_I4:  u = (unsigned long long)(long long)V_I4(v);  break;
        case VT_I2:  u = (unsigned long long)(short)V_I2(v);      break;
        case VT_I8:  u = (unsigned long long)V_I8(v);             break;
        case VT_INT: u = (unsigned long long)(int)V_INT(v);       break;
        case VT_UI8: u = V_UI8(v); break;
        case VT_UI4: u = V_UI4(v); break;
        case VT_UI2: u = V_UI2(v); break;
        default:     u = V_UINT(v); break;
        }
        snprintf(buf, sizeof buf, "%llu", u);
        return buf;
    }
    case VT_R4: { char b[32]; snprintf(b, sizeof b, "%.2f", (double)V_R4(v)); return b; }
    case VT_R8: { char b[32]; snprintf(b, sizeof b, "%.2f", V_R8(v)); return b; }
    default: break;
    }
    VARIANT tmp; VariantInit(&tmp);
    if (SUCCEEDED(VariantChangeType(&tmp, v, 0, VT_BSTR)) && V_BSTR(&tmp))
        return win::wide_to_utf8(V_BSTR(&tmp));
    VariantClear(&tmp);
    return {};
}

std::uint64_t variant_to_u64(void* pv) {
    if (!pv) return 0;
    VARIANT* v = static_cast<VARIANT*>(pv);
    VARIANT tmp; VariantInit(&tmp);
    if (SUCCEEDED(VariantChangeType(&tmp, v, 0, VT_UI8)))
        return V_UI8(&tmp);
    return 0;
}

Session::Session() {
    com_ = std::make_unique<win::ComScope>();   // WMI needs COM per thread
}

Session::~Session() {
    if (service_) static_cast<IWbemServices*>(service_)->Release();
    if (locator_) static_cast<IWbemLocator*>(locator_)->Release();
}

bool Session::connect(const wchar_t* ns) {
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator,
                                  reinterpret_cast<void**>(&locator_));
    if (FAILED(hr) || !locator_) return false;

    BSTR res_name = bstr_from(ns);
    hr = static_cast<IWbemLocator*>(locator_)->ConnectServer(
        res_name, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
        reinterpret_cast<IWbemServices**>(&service_));
    SysFreeString(res_name);
    if (FAILED(hr) || !service_) return false;

    hr = CoSetProxyBlanket(static_cast<IWbemServices*>(service_), RPC_C_AUTHN_WINNT,
                           RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    return SUCCEEDED(hr);
}

bool Session::query(const std::wstring& wql,
                    const std::vector<std::wstring>& props,
                    std::vector<std::vector<std::string>>& rows) {
    if (!service_) return false;
    IWbemServices* svc = static_cast<IWbemServices*>(service_);

    BSTR lang = bstr_from(L"WQL");
    BSTR text = bstr_from(wql.c_str());
    IEnumWbemClassObject* en = nullptr;
    HRESULT hr = svc->ExecQuery(lang, text,
                                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                nullptr, &en);
    SysFreeString(lang);
    SysFreeString(text);
    if (FAILED(hr) || !en) return false;

    while (true) {
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        hr = en->Next(WBEM_INFINITE, 1, &obj, &got);
        if (hr == WBEM_S_FALSE || got == 0 || FAILED(hr)) break;

        std::vector<std::string> row(props.size());
        for (size_t i = 0; i < props.size(); ++i) {
            VARIANT val;
            VariantInit(&val);
            HRESULT gr = obj->Get(props[i].c_str(), 0, &val, nullptr, nullptr);
            if (SUCCEEDED(gr) && val.vt != VT_NULL && val.vt != (VT_ARRAY | VT_BSTR))
                row[i] = variant_to_utf8(&val);
            VariantClear(&val);
        }
        rows.push_back(std::move(row));
        obj->Release();
        if (rows.size() > 4096) break;
    }
    en->Release();
    return true;
}

bool Session::query_one(const std::wstring& wql,
                        const std::vector<std::wstring>& props,
                        std::vector<std::string>& row) {
    std::vector<std::vector<std::string>> rows;
    if (!query(wql, props, rows) || rows.empty()) return false;
    row = rows.front();
    return true;
}

} // namespace wmi
} // namespace krad

#endif // _WIN32

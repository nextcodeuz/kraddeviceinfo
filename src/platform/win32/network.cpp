// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - network adapters collector (win32)
#ifndef WINVER_LEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "../../core/collect.h"
#include "wincompat.h"
#include "../../core/util.h"

#ifdef _WIN32

#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace krad {
namespace collect {

using win::wide_to_utf8;

static std::string sockaddr_to_str(const SOCKET_ADDRESS& sa) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (sa.lpSockaddr->sa_family == AF_INET)
        inet_ntop(AF_INET,
                  &reinterpret_cast<sockaddr_in*>(sa.lpSockaddr)->sin_addr,
                  buf, sizeof buf);
    else if (sa.lpSockaddr->sa_family == AF_INET6)
        inet_ntop(AF_INET6,
                  &reinterpret_cast<sockaddr_in6*>(sa.lpSockaddr)->sin6_addr,
                  buf, sizeof buf);
    return buf;
}

static const char* if_type_name(std::uint32_t t) {
    switch (t) {
    case IF_TYPE_ETHERNET_CSMACD: return "Ethernet";
    case IF_TYPE_SOFTWARE_LOOPBACK: return "Loopback";
    case IF_TYPE_IEEE80211: return "Wi-Fi";   // 71
    case 131: return "Tunnel";
    case 237: return "Virtual Ethernet";
    default:  return "Other";
    }
}
static const char* oper_state_name(int s) {
    switch (s) {
    case IfOperStatusUp: return "Up";
    case IfOperStatusDown: return "Down";
    case IfOperStatusTesting: return "Testing";
    case IfOperStatusUnknown: return "Unknown";
    case IfOperStatusDormant: return "Dormant";
    case IfOperStatusNotPresent: return "Not present";
    case IfOperStatusLowerLayerDown: return "Lower layer down";
    default: return "?";
    }
}

std::vector<NetAdapter> network_adapters() {
    std::vector<NetAdapter> out;

    // gateways from IPv4 forwarding table (works XP..11)
    std::map<DWORD, std::string> gw;
    {
        MIB_IPFORWARDTABLE* ft = nullptr;
        ULONG sz = 0;
        if (GetIpForwardTable(nullptr, &sz, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
            ft = reinterpret_cast<MIB_IPFORWARDTABLE*>(malloc(sz));
            if (ft && GetIpForwardTable(ft, &sz, FALSE) == NO_ERROR)
                for (UINT i = 0; i < ft->dwNumEntries; ++i) {
                    auto& e = ft->table[i];
                    if (e.dwForwardDest == 0 && e.dwForwardMask == 0) {
                        in_addr a; a.S_un.S_addr = e.dwForwardNextHop;
                        char b[32] = {0};
                        inet_ntop(AF_INET, &a, b, sizeof b);
                        gw[e.dwForwardIfIndex] = b;
                    }
                }
            free(ft);
        }
    }

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_FRIENDLY_NAME;
    ULONG sz = 15 * 1024;
    std::vector<char> buf;
    for (int tries = 0; tries < 3; ++tries) {
        buf.resize(sz);
        PIP_ADAPTER_ADDRESSES aa =
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        ULONG r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, aa, &sz);
        if (r == NO_ERROR) break;
        if (r != ERROR_BUFFER_OVERFLOW || tries == 2) return out;
    }

    for (auto* aa = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
         aa; aa = aa->Next) {
        NetAdapter a;
        a.if_index = int(aa->IfIndex);
        a.name = wide_to_utf8(aa->FriendlyName);
        a.description = wide_to_utf8(aa->Description);

        wchar_t mb[64];
        if (aa->PhysicalAddressLength &&
            aa->PhysicalAddressLength <= 8) {
            std::string mac;
            char tmp[8];
            for (ULONG i = 0; i < aa->PhysicalAddressLength; ++i) {
                snprintf(tmp, sizeof tmp, "%02X%s", aa->PhysicalAddress[i],
                         i + 1 < aa->PhysicalAddressLength ? ":" : "");
                mac += tmp;
            }
            a.mac = mac;
        }

        for (auto* u = aa->FirstUnicastAddress; u; u = u->Next) {
            std::string ip = sockaddr_to_str(u->Address);
            if (u->Address.lpSockaddr->sa_family == AF_INET && a.ip4.empty()) {
                a.ip4 = ip;
                if (u->OnLinkPrefixLength > 0 && u->OnLinkPrefixLength <= 32) {
                    std::uint32_t mask = htonl(~0u << (32 - u->OnLinkPrefixLength));
                    in_addr m; m.S_un.S_addr = mask;
                    char b2[32] = {0};
                    inet_ntop(AF_INET, &m, b2, sizeof b2);
                    a.subnet_mask = b2;
                }
            } else if (u->Address.lpSockaddr->sa_family == AF_INET6 &&
                       !u->SuffixOrigin) {
                if (a.ip6.empty() && !contains_ci(ip, "fe80")) a.ip6 = ip;
            }
        }

        for (auto* d = aa->FirstDnsServerAddress; d; d = d->Next) {
            std::string dns = sockaddr_to_str(d->Address);
            if (!dns.empty())
                a.dns_servers += (a.dns_servers.empty() ? "" : ", ") + dns;
        }

        a.gateway = gw.count(aa->IfIndex) ? gw[aa->IfIndex] : "";

        a.link_speed_bps = std::uint64_t(aa->TransmitLinkSpeed);   // bits/s
        a.adapter_type   = if_type_name(aa->IfType);
        a.state          = oper_state_name(aa->OperStatus);
        out.push_back(std::move(a));
    }

    // DHCP info from GetAdaptersInfo (works on every Windows version)
    {
        ULONG sz_info = 0;
        if (GetAdaptersInfo(nullptr, &sz_info) == ERROR_BUFFER_OVERFLOW && sz_info) {
            std::vector<char> ibuf(sz_info);
            PIP_ADAPTER_INFO ai =
                reinterpret_cast<PIP_ADAPTER_INFO>(ibuf.data());
            if (GetAdaptersInfo(ai, &sz_info) == NO_ERROR)
                for (; ai; ai = ai->Next) {
                    for (auto& a : out) {
                        if (a.if_index != int(ai->Index)) continue;
                        a.dhcp_enabled = ai->DhcpEnabled != 0;
                        if (a.dhcp_enabled && ai->DhcpServer.IpAddress.String[0])
                            a.dhcp_server = ai->DhcpServer.IpAddress.String;
                        if (a.dhcp_enabled && ai->LeaseExpires &&
                            ai->LeaseExpires != 0xFFFFFFFFu) {
                            time_t le = time_t(ai->LeaseExpires);
                            char tb[32];
                            struct tm tmv;
                            localtime_s(&tmv, &le);
                            strftime(tb, sizeof tb, "%Y-%m-%d %H:%M", &tmv);
                            a.lease_expires = tb;
                        }
                    }
                }
        }
    }

    // cumulative traffic counters via GetIfEntry
    PMIB_IFTABLE t1 = nullptr;
    ULONG sz1 = 0;
    if (GetIfTable(nullptr, &sz1, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        t1 = static_cast<PMIB_IFTABLE>(malloc(sz1));
        if (t1 && GetIfTable(t1, &sz1, FALSE) == NO_ERROR)
            for (UINT i = 0; i < t1->dwNumEntries; ++i) {
                auto& row = t1->table[i];
                for (auto& a : out)
                    if (a.if_index == int(row.dwIndex)) {
                        a.rx_bytes = row.dwInOctets;
                        a.tx_bytes = row.dwOutOctets;
                    }
            }
        free(t1);
    }
    return out;
}

} // namespace collect
} // namespace krad

#endif // _WIN32

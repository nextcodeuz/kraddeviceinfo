// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - exporters implementation
#include "export.h"
#include "../core/util.h"
#include <krad/model.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <ctime>

namespace krad {
namespace export_ {

// ---------------------------------------------------------------- json
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += char(c);
        }
    }
    return out;
}

static void j_kv(std::ostringstream& o, const std::string& k,
                 const std::string& v, bool comma = true) {
    if (!v.empty() && v != "-")
        o << "    \"" << json_escape(k) << "\": \"" << json_escape(v) << "\""
          << (comma ? "," : "") << "\n";
}

std::string to_json(const DeviceReport& r) {
    std::ostringstream o;
    o << "{\n  \"app\": \"" << krad::APP_NAME << "\",\n";
    o << "  \"id\": \"" << krad::APP_ID << "\",\n";
    o << "  \"version\": \"" << krad::APP_VERSION << "\",\n";
    o << "  \"generated\": \"" << json_escape(r.generated_at) << "\",\n";

    o << "  \"operating_system\": {\n";
    j_kv(o, "product", r.os.product_name);
    j_kv(o, "edition", r.os.edition);
    j_kv(o, "display_version", r.os.display_version);
    j_kv(o, "build", r.os.build_string);
    j_kv(o, "architecture", r.os.architecture);
    j_kv(o, "install_date", r.os.install_date);
    j_kv(o, "uptime_seconds", std::to_string(r.os.uptime_sec));
    j_kv(o, "locale", r.os.locale);
    j_kv(o, "timezone", r.os.timezone);
    j_kv(o, "processes", std::to_string(r.os.process_count));
    j_kv(o, "threads", std::to_string(r.os.thread_count));
    o << "  },\n";

    o << "  \"computer\": {\n";
    j_kv(o, "hostname", r.computer.hostname);
    j_kv(o, "user", r.computer.username);
    j_kv(o, "manufacturer", r.computer.manufacturer);
    j_kv(o, "model", r.computer.model);
    j_kv(o, "domain", r.computer.domain);
    j_kv(o, "workgroup", r.computer.workgroup);
    j_kv(o, "chassis", r.computer.chassis_type);
    j_kv(o, "uuid", r.computer.uuid);
    o << "  },\n";

    o << "  \"bios\": {\n";
    j_kv(o, "vendor", r.bios.vendor);
    j_kv(o, "version", r.bios.version);
    j_kv(o, "date", r.bios.date);
    j_kv(o, "mode", r.bios.mode);
    j_kv(o, "secure_boot", r.bios.secure_boot);
    j_kv(o, "board_manufacturer", r.bios.baseboard_manufacturer);
    j_kv(o, "board_product", r.bios.baseboard_product);
    j_kv(o, "board_version", r.bios.baseboard_version);
    j_kv(o, "board_serial", r.bios.baseboard_serial);
    o << "  },\n";

    o << "  \"cpu\": {\n";
    j_kv(o, "brand", r.cpu.brand);
    j_kv(o, "vendor", r.cpu.vendor);
    j_kv(o, "code_name", r.cpu.code_name);
    j_kv(o, "socket", r.cpu.socket);
    j_kv(o, "physical_cores", std::to_string(r.cpu.cores_physical));
    j_kv(o, "logical_cores", std::to_string(r.cpu.cores_logical));
    j_kv(o, "family_model_stepping", r.cpu.family_model_str);
    j_kv(o, "base_clock_mhz", r.cpu.base_clock_mhz ?
           std::to_string(int(r.cpu.base_clock_mhz)) : "");
    j_kv(o, "max_clock_mhz", r.cpu.max_clock_mhz ?
           std::to_string(int(r.cpu.max_clock_mhz)) : "");
    {
        std::string feat;
        for (size_t i = 0; i < r.cpu.features.size(); ++i)
            feat += (i ? ", " : "") + r.cpu.features[i];
        j_kv(o, "instructions", feat);
    }
    o << "    \"caches\": [\n";
    for (size_t i = 0; i < r.cpu.caches.size(); ++i) {
        auto& c = r.cpu.caches[i];
        o << "      {\"level\": \"" << c.level << "\", \"size_kb\": "
          << c.size_kb << "}" << (i + 1 < r.cpu.caches.size() ? "," : "") << "\n";
    }
    o << "    ]\n  },\n";

    o << "  \"memory\": {\n";
    j_kv(o, "total_bytes", std::to_string(r.memory.total_phys));
    j_kv(o, "available_bytes", std::to_string(r.memory.avail_phys));
    o << "    \"modules\": [\n";
    for (size_t i = 0; i < r.memory.modules.size(); ++i) {
        auto& m = r.memory.modules[i];
        o << "      {\"slot\": \"" << json_escape(m.slot)
          << "\", \"bytes\": " << m.capacity_bytes
          << ", \"type\": \"" << m.type
          << "\", \"speed_mts\": " << m.speed_mtps
          << ", \"manufacturer\": \"" << json_escape(m.manufacturer)
          << "\", \"part_number\": \"" << json_escape(m.part_number)
          << "\"}" << (i + 1 < r.memory.modules.size() ? "," : "") << "\n";
    }
    o << "    ]\n  },\n";

    auto arr_open = [&](const char* name) {
        o << "  \"" << name << "\": [\n";
    };
    auto item_end = [&](size_t i, size_t n) {
        o << (i + 1 < n ? "    },\n" : "    }\n");
    };

    arr_open("gpus");
    for (size_t i = 0; i < r.gpus.size(); ++i) {
        auto& g = r.gpus[i];
        o << "    {\n";
        j_kv(o, "name", g.name);
        j_kv(o, "vendor", g.vendor);
        j_kv(o, "vram_bytes", g.vram_bytes ? std::to_string(g.vram_bytes) : "");
        j_kv(o, "driver_version", g.driver_version);
        j_kv(o, "video_mode", g.video_mode);
        o << "    }";
        o << (i + 1 < r.gpus.size() ? ",\n" : "\n");
    }
    o << (r.gpus.empty() ? "  ],\n" : "  ],\n");

    arr_open("disks");
    for (size_t i = 0; i < r.disks.size(); ++i) {
        auto& d = r.disks[i];
        o << "    {\n";
        j_kv(o, "model", d.model);
        j_kv(o, "serial", d.serial);
        j_kv(o, "interface", d.iface);
        j_kv(o, "media_type", d.media_type);
        j_kv(o, "size_bytes", d.size_bytes ? std::to_string(d.size_bytes) : "");
        j_kv(o, "health", d.health);
        o << "    }";
        o << (i + 1 < r.disks.size() ? ",\n" : "\n");
    }
    o << "  ],\n";

    arr_open("volumes");
    for (size_t i = 0; i < r.volumes.size(); ++i) {
        auto& v = r.volumes[i];
        o << "    {\n";
        j_kv(o, "letter", v.letter);
        j_kv(o, "label", v.label);
        j_kv(o, "fs", v.fs);
        j_kv(o, "total_bytes", std::to_string(v.total_bytes));
        j_kv(o, "free_bytes", std::to_string(v.free_bytes));
        o << "    }";
        o << (i + 1 < r.volumes.size() ? ",\n" : "\n");
    }
    o << "  ],\n";

    arr_open("network_adapters");
    for (size_t i = 0; i < r.adapters.size(); ++i) {
        auto& a = r.adapters[i];
        o << "    {\n";
        j_kv(o, "name", a.name);
        j_kv(o, "description", a.description);
        j_kv(o, "mac", a.mac);
        j_kv(o, "ipv4", a.ip4);
        j_kv(o, "gateway", a.gateway);
        j_kv(o, "link_speed_bps", a.link_speed_bps ?
               std::to_string(a.link_speed_bps) : "");
        j_kv(o, "state", a.state);
        o << "    }";
        o << (i + 1 < r.adapters.size() ? ",\n" : "\n");
    }
    o << "  ],\n";

    arr_open("monitors");
    for (size_t i = 0; i < r.monitors.size(); ++i) {
        auto& m = r.monitors[i];
        o << "    {\n";
        j_kv(o, "name", m.name);
        j_kv(o, "manufacturer", m.manufacturer);
        j_kv(o, "serial", m.serial);
        j_kv(o, "native_resolution", m.native_resolution);
        j_kv(o, "current_mode", m.current_mode);
        o << "    }";
        o << (i + 1 < r.monitors.size() ? ",\n" : "\n");
    }
    o << "  ]\n";

    o << "}\n";

    // strip trailing commas before closing braces/brackets (strict JSON)
    std::string raw = o.str();
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == ',') {
            size_t j = i + 1;
            while (j < raw.size() &&
                   (raw[j] == ' ' || raw[j] == '\n' || raw[j] == '\r' ||
                    raw[j] == '\t'))
                ++j;
            if (j < raw.size() && (raw[j] == '}' || raw[j] == ']'))
                continue;                       // drop the comma
        }
        out += raw[i];
    }
    return out;
}

// ---------------------------------------------------------------- html
static std::string html_escape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
        case '<':  out += "&lt;";  break;
        case '>':  out += "&gt;";  break;
        case '&':  out += "&amp;"; break;
        case '"':  out += "&quot;";break;
        default:   out += ch;
        }
    }
    return out;
}

std::string to_html(const DeviceReport& r, const std::vector<PerfSeriesPoint>&) {
    std::ostringstream h;

    h << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      << "<title>KradDeviceInfo Report</title>\n"
      << R"(<style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--txt:#e6edf3;--dim:#8b949e;
--accent:#58a6ff;--ok:#3fb950;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.5
'Segoe UI',system-ui,sans-serif;padding:32px}
.wrap{max-width:1080px;margin:0 auto}
h1{font-size:26px;font-weight:600;margin:0 0 4px}
h1 span{color:var(--accent)}
.sub{color:var(--dim);margin-bottom:28px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;
padding:18px;break-inside:avoid}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.08em;
color:var(--accent);margin:0 0 12px}
table{width:100%;border-collapse:collapse}
td{padding:4px 6px;vertical-align:top}
td.k{color:var(--dim);white-space:nowrap;width:42%}
tr:nth-child(even){background:#ffffff08}
.badge{display:inline-block;background:#1f6feb33;border:1px solid #1f6feb88;
color:var(--accent);border-radius:20px;padding:1px 10px;font-size:12px}
.footer{color:var(--dim);font-size:12px;margin-top:28px;text-align:center}
</style>)"
      << "</head>\n<body><div class=\"wrap\">\n";

    h << "<h1>Krad<span>DeviceInfo</span></h1>\n<div class=\"sub\">"
      << html_escape(r.os.product_name) + " &bull; " +
         html_escape(r.cpu.brand) + " &bull; " +
         format_bytes(r.memory.total_phys) +
         " &nbsp;<span class=\"badge\">" + html_escape(r.generated_at) +
         "</span></div>\n";

    for (const auto& s : r.sections()) {
        h << "<div class=\"card\"><h2>" << html_escape(s.title) << "</h2>"
          << "<table>";
        for (const auto& row : s.rows)
            h << "<tr><td class=\"k\">" << html_escape(row.key)
              << "</td><td>" << html_escape(row.value) << "</td></tr>";
        h << "</table></div>\n";
    }

    h << "<div class=\"footer\">Generated by " << APP_NAME << " v"
      << APP_VERSION << " (" << APP_ID << ")</div>\n</div></body></html>\n";
    return h.str();
}

// ---------------------------------------------------------------- txt
std::string to_txt(const DeviceReport& r) {
    std::ostringstream t;
    t << "======================================================================\n";
    t << "  KRAD DEVICE INFO  v" << APP_VERSION << "   (" << APP_ID << ")\n";
    t << "  Generated: " << r.generated_at << "\n";
    t << "======================================================================\n\n";
    for (const auto& s : r.sections()) {
        t << "--- " << s.title << " ";
        t << std::string(std::max(2, int(68 - s.title.size())), '-') << "\n";
        size_t wkey = 0;
        for (auto& row : s.rows) wkey = std::max(wkey, row.key.size());
        for (auto& row : s.rows)
            t << "  " << row.key
              << std::string(wkey - row.key.size(), ' ') << " : "
              << row.value << "\n";
        t << "\n";
    }
    return t.str();
}

// ---------------------------------------------------------------- csv
static std::string csv_escape(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"') out += "\"\"";
        else out += ch;
    }
    return out + "\"";
}

std::string to_csv(const DeviceReport& r) {
    std::ostringstream c;
    c << "section,key,value\n";
    for (const auto& s : r.sections())
        for (const auto& row : s.rows)
            c << csv_escape(s.title) << "," << csv_escape(row.key) << ","
              << csv_escape(row.value) << "\n";
    return c.str();
}

// ---------------------------------------------------------------- writers
static bool write_file(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), std::streamsize(data.size()));
    return bool(f);
}

bool write_json(const DeviceReport& r, const std::string& p)
{ return write_file(p, to_json(r)); }
bool write_html(const DeviceReport& r, const std::string& p,
                const std::vector<PerfSeriesPoint>& hist)
{ return write_file(p, to_html(r, hist)); }
bool write_txt(const DeviceReport& r, const std::string& p)
{ return write_file(p, to_txt(r)); }
bool write_csv(const DeviceReport& r, const std::string& p)
{ return write_file(p, to_csv(r)); }

} // namespace export_
} // namespace krad

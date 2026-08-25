// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - online services implementation
#include "online.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QDataStream>
#include <QRandomGenerator>
#include <QDateTime>
#include <QDataStream>
#include <thread>
#include <atomic>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace krad {

static QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "KradDeviceInfo/1.1");
    req.setTransferTimeout(20000);
    return req;
}

OnlineServices::OnlineServices(QObject* parent) : QObject(parent) {}

// QNAM created lazily: constructing it early can interfere with
// QTcpServer socket notifiers in some environments.
QNetworkAccessManager* OnlineServices::nam() {
    if (!nam_) nam_ = new QNetworkAccessManager(this);
    return nam_;
}

// ---------------------------------------------------------------- ip/geo
void OnlineServices::fetchIpInfo() {
    QNetworkReply* rep =
        nam()->get(makeRequest(QUrl("https://ipwho.is/")));
    QObject::connect(rep, &QNetworkReply::finished, this, [this, rep] {
        IpGeoInfo info;
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) {
            info.error = rep->errorString();
            emit ipInfoReady(info);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(rep->readAll());
        if (!doc.isObject()) { info.error = "bad json"; emit ipInfoReady(info); return; }
        QJsonObject o = doc.object();
        info.ok = o["success"].toBool(true);
        info.ip          = o["ip"].toString();
        info.city        = o["city"].toString();
        info.region      = o["region"].toString();
        info.country     = o["country"].toString();
        info.countryCode = o["country_code"].toString();
        info.timezone    = o["timezone"].toObject()["id"].toString();
        QJsonObject conn = o["connection"].toObject();
        info.isp = conn["isp"].toString();
        info.org = conn["org"].toString();
        info.asn = conn["asn"].toString();
        emit ipInfoReady(info);
    });
}

// ---------------------------------------------------------------- speed
void OnlineServices::runSpeedTest() {
    if (speed_phase_ != Idle) return;
    speed_result_ = {};
    speed_phase_ = Pinging;
    ping_rounds_left_ = 3; ping_sum_ms_ = 0; ping_count_ = 0;
    emit speedStage(tr("Pinging Cloudflare edge..."));
    speedPingRound();
}

void OnlineServices::speedPingRound() {
    QElapsedTimer* t = new QElapsedTimer();
    t->start();
    QNetworkReply* rep = nam()->get(makeRequest(
        QUrl("https://speed.cloudflare.com/__down?bytes=0")));
    QObject::connect(rep, &QNetworkReply::finished, this, [this, rep, t] {
        rep->deleteLater();
        qint64 ms = t->elapsed();
        delete t;
        if (ms > 0 && ms < 5000) { ping_sum_ms_ += ms; ++ping_count_; }
        if (--ping_rounds_left_ > 0) { speedPingRound(); return; }
        speed_result_.ping_ms = ping_count_ ? ping_sum_ms_ / ping_count_ : 0;

        speed_phase_ = Downloading;
        dl_bytes_ = 0;
        emit speedStage(tr("Download test (25 MB from Cloudflare)..."));
        speedDownloadRound();
    });
}

void OnlineServices::speedDownloadRound() {
    QElapsedTimer* t = new QElapsedTimer();
    t->start();
    dl_start_ = 0;
    QNetworkReply* rep = nam()->get(makeRequest(
        QUrl("https://speed.cloudflare.com/__down?bytes=26214400")));
    QObject::connect(rep, &QNetworkReply::downloadProgress, this,
            [this, t](qint64 recv, qint64) {
        dl_bytes_ = recv;
        double sec = t->elapsed() / 1000.0;
        if (sec > 0.2) {
            double mbps = recv * 8 / sec / 1e6;
            emit speedProgress(mbps);
        }
    });
    QObject::connect(rep, &QNetworkReply::finished, this, [this, rep, t] {
        rep->abort();
        rep->deleteLater();
        double sec = t->elapsed() / 1000.0;
        delete t;
        if (sec > 0.05 && dl_bytes_ > 0)
            down_mbps_ = dl_bytes_ * 8 / sec / 1e6;
        speed_result_.down_mbps = down_mbps_;

        speed_phase_ = Uploading;
        emit speedStage(tr("Upload test (8 MB to Cloudflare)..."));
        speedUploadRound();
    });
    // safety cap: 20 s
    QTimer::singleShot(20000, rep, &QNetworkReply::abort);
}

void OnlineServices::speedUploadRound() {
    QByteArray payload;
    payload.resize(8 * 1024 * 1024);
    quint32 seed = QRandomGenerator::global()->generate();
    for (int i = 0; i < payload.size(); ++i) {           // cheap PRNG fill
        seed = seed * 1664525u + 1013904223u;
        payload[i] = char(seed >> 24);
    }
    QElapsedTimer* t = new QElapsedTimer();
    QNetworkRequest req = makeRequest(
        QUrl("https://speed.cloudflare.com/__up"));
    req.setRawHeader("Content-Type", "application/octet-stream");
    t->start();
    up_start_ = 0;
    QNetworkReply* rep = nam()->post(req, payload);
    QObject::connect(rep, &QNetworkReply::uploadProgress, this,
            [this, t](qint64 sent, qint64) {
        up_bytes_ = sent;
        double sec = t->elapsed() / 1000.0;
        if (sec > 0.2) emit speedProgress(sent * 8 / sec / 1e6);
    });
    QObject::connect(rep, &QNetworkReply::finished, this, [this, rep, t, payload] {
        auto err = rep->error();
        rep->deleteLater();
        double sec = t->elapsed() / 1000.0;
        delete t;
        if (up_bytes_ == 0 && err == QNetworkReply::NoError)
            up_bytes_ = payload.size();               // progress never fired
        if (sec > 0.05 && up_bytes_ > 0 &&
            err == QNetworkReply::NoError)
            up_mbps_ = up_bytes_ * 8 / sec / 1e6;
        speed_result_.up_mbps = up_mbps_;
        speed_result_.ok = speed_result_.down_mbps > 0;
        speed_phase_ = Idle;
        emit speedStage({});
        emit speedDone(speed_result_);
    });
}

// ---------------------------------------------------------------- dns
static QByteArray build_dns_query(quint16 txid, const QByteArray& qname) {
    QByteArray q;
    q.append(char(txid >> 8)); q.append(char(txid & 0xFF));
    q.append(char(0x01)); q.append(char(0x00));      // RD=1
    q.append(char(0x00)); q.append(char(0x01));      // QDCOUNT=1
    for (int i = 0; i < 6; ++i) q.append(char(0));   // AN/NS/ARCOUNT=0
    QDataStream ds(&q, QIODevice::WriteOnly | QIODevice::Append);
    for (const QByteArray& part : qname.split('.')) {
        ds << quint8(part.size());
        ds.writeRawData(part.constData(), part.size());
    }
    ds << quint8(0);
    ds << quint16(1);   // type A
    ds << quint16(1);   // class IN
    return q;
}

void OnlineServices::runDnsBenchmark() {
    // All servers are probed CONCURRENTLY (one thread each); a server is
    // marked unreachable after 2 consecutive timeouts so a blocked-UDP
    // sandbox finishes in ~3 s instead of 30 s.
    dns_servers_ = {
        {"1.1.1.1",         "Cloudflare"},
        {"8.8.8.8",         "Google"},
        {"9.9.9.9",         "Quad9"},
        {"208.67.222.222",  "OpenDNS"},
    };
    dns_results_.clear();
    emit dnsProgress(0, int(dns_servers_.size()));

    auto results = std::make_shared<std::vector<DnsServerResult>>();
    auto remaining = std::make_shared<std::atomic<int>>(int(dns_servers_.size()));

    for (size_t i = 0; i < dns_servers_.size(); ++i) {
        const QString server = dns_servers_[i].first;
        const QString name   = dns_servers_[i].second;
        std::thread([this, server, name, results, remaining] {
            DnsServerResult r;
            r.server = server;
            r.name   = name;
            r.total  = 5;
            int consecutive_timeouts = 0;
            for (int q = 0; q < 5; ++q) {
                auto* sock = new QUdpSocket();
                quint16 txid = quint16(
                    QRandomGenerator::global()->generate() & 0xFFFF);
                QString qname = QString("%1.example.com")
                    .arg(QRandomGenerator::global()->generate() & 0xFFFFFF,
                         6, 16, QChar('0'));
                QByteArray query = build_dns_query(txid, qname.toLatin1());
                QHostAddress target(server);
                QElapsedTimer t;
                t.start();
                bool got = sock->writeDatagram(query, target, 53) > 0 &&
                           sock->waitForReadyRead(1200) &&
                           sock->hasPendingDatagrams();
                double ms = t.elapsed();
                QByteArray resp = got ? sock->readAll() : QByteArray();
                sock->deleteLater();
                if (got && resp.size() >= 12 &&
                    quint8(resp[0]) == quint8(txid >> 8) &&
                    quint8(resp[1]) == quint8(txid & 0xFF)) {
                    ++r.ok_count;
                    r.avg_ms += ms;
                    if (ms < r.min_ms) r.min_ms = ms;
                    consecutive_timeouts = 0;
                } else if (++consecutive_timeouts >= 2) {
                    break;                       // fast-fail unreachable
                }
            }
            if (r.ok_count) r.avg_ms /= r.ok_count;
            else r.min_ms = 0;
            results->push_back(r);
            QMetaObject::invokeMethod(this, [this, remaining, results] {
                emit dnsProgress(
                    int(dns_servers_.size() - remaining->load()) + 1,
                    int(dns_servers_.size()));
                if (remaining->fetch_sub(1) == 1) {
                    // keep a stable order: as declared
                    std::vector<DnsServerResult> ordered;
                    for (auto& s : dns_servers_)
                        for (auto& r : *results)
                            if (r.server == s.first) ordered.push_back(r);
                    dns_results_ = std::move(ordered);
                    emit dnsDone(dns_results_);
                }
            }, Qt::QueuedConnection);
        }).detach();
    }
}

void OnlineServices::dnsSendQuery() {}   // legacy hook (unused)

// ---------------------------------------------------------------- ntp
void OnlineServices::fetchNtpOffset() {
    auto* sock = new QUdpSocket(this);
    QHostInfo::lookupHost("pool.ntp.org", this,
        [this, sock](const QHostInfo& info) {
            if (info.addresses().isEmpty()) {
                sock->deleteLater();
                emit ntpDone(false, 0, "DNS lookup failed");
                return;
            }
            QHostAddress addr = info.addresses().first();
            QByteArray pkt(48, char(0));
            pkt[0] = char(0x1B);                     // LI=0, VN=3, Mode=3
            QElapsedTimer t;
            t.start();
            qint64 sent = sock->writeDatagram(pkt, addr, 123);
            if (sent < 0 || !sock->waitForReadyRead(2500) ||
                !sock->hasPendingDatagrams()) {
                sock->deleteLater();
                emit ntpDone(false, 0, "no reply (timeout or UDP blocked)");
                return;
            }
            double rtt_ms = t.elapsed();
            QByteArray resp = sock->readAll();
            sock->deleteLater();
            if (resp.size() < 48) { emit ntpDone(false, 0, "short reply"); return; }

            auto read_ts = [&](int off) -> double {  // NTP -> unix seconds
                quint32 s = (quint8(resp[off]) << 24) |
                            (quint8(resp[off+1]) << 16) |
                            (quint8(resp[off+2]) << 8) | quint8(resp[off+3]);
                quint32 f = (quint8(resp[off+4]) << 24) |
                            (quint8(resp[off+5]) << 16) |
                            (quint8(resp[off+6]) << 8) | quint8(resp[off+7]);
                return (s - 2208988800u) + f / 4294967296.0;
            };
            double t2 = read_ts(32);                 // server receive
            double t3 = read_ts(40);                 // server transmit
            double t0 = (QDateTime::currentMSecsSinceEpoch() - rtt_ms) / 1000.0;
            double t1 = t0 + rtt_ms / 2000.0;        // approx client send/recv
            double offset_s = ((t1 - t0) + (t2 - t3)) / 2.0;
            emit ntpDone(true, offset_s * 1000.0,
                         addr.toString() + " (pool.ntp.org)");
        });
}

// ---------------------------------------------------------------- updates
void OnlineServices::checkForUpdates(const QString& currentVersion) {
    QNetworkReply* rep = nam()->get(makeRequest(QUrl(VERSION_URL)));
    QObject::connect(rep, &QNetworkReply::finished, this,
        [this, rep, currentVersion] {
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) {
            emit updateCheckDone(false, {}, {}, rep->errorString());
            return;
        }
        QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
        QString latest = o["version"].toString();
        QString url    = o["url"].toString();
        QString notes  = o["notes"].toString();

        auto toNums = [](const QString& v) {
            QList<qint64> parts;
            for (const QString& p : v.split('.'))
                parts << p.toLongLong();
            while (parts.size() < 3) parts << 0;
            return parts;
        };
        QList<qint64> cur = toNums(currentVersion);
        QList<qint64> lat = toNums(latest);
        bool newer = lat.size() == 3 && cur.size() == 3 &&
                     (lat[0] > cur[0] ||
                      (lat[0] == cur[0] && lat[1] > cur[1]) ||
                      (lat[0] == cur[0] && lat[1] == cur[1] && lat[2] > cur[2]));
        emit updateCheckDone(newer, latest, notes,
                             latest.isEmpty() ? "invalid manifest" : QString());
    });
}

// ---------------------------------------------------------------- share
void OnlineServices::shareReport(const QString& text) {
    // primary: dpaste.org ; fallback: paste.rs
    QUrlQuery form;
    form.addQueryItem("content", text);
    form.addQueryItem("lexer", "text");
    form.addQueryItem("format", "json");
    form.addQueryItem("expires", "never");

    QNetworkRequest req(QUrl("https://dpaste.org/api/"));
    req.setRawHeader("User-Agent", "KradDeviceInfo/1.1");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    QNetworkReply* rep = nam()->post(req, form.toString(QUrl::FullyEncoded).toUtf8());

    QObject::connect(rep, &QNetworkReply::finished, this, [this, rep, text] {
        if (rep->error() == QNetworkReply::NoError) {
            QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
            QString url = o["url"].toString();
            if (!url.isEmpty()) {
                rep->deleteLater();
                emit reportShared(true, url);
                return;
            }
        }
        rep->deleteLater();
        // fallback: paste.rs returns the URL as plain text body
        QNetworkReply* rep2 = nam()->post(
            QNetworkRequest(QUrl("https://paste.rs/")),
            text.toUtf8());
        QObject::connect(rep2, &QNetworkReply::finished, this, [this, rep2] {
            rep2->deleteLater();
            if (rep2->error() == QNetworkReply::NoError) {
                QString url = QString::fromUtf8(rep2->readAll()).trimmed();
                if (url.startsWith("http")) { emit reportShared(true, url); return; }
            }
            emit reportShared(false, rep2->errorString());
        });
    });
}

// ---------------------------------------------------------------- dashboard
void OnlineServices::setStatsProvider(std::function<QByteArray()> p) {
    stats_provider_ = std::move(p);
}
void OnlineServices::setReportText(const QString& t) { report_text_ = t; }

bool OnlineServices::startDashboard(quint16 port) {
    stopDashboard();
    if (!dash_server_) {
        dash_server_ = new QTcpServer(this);
        auto handle = [this](QTcpSocket* c) {
            QByteArray req = c->peek(2048);
            QString path = "/";
            int sp1 = req.indexOf(' ');
            int sp2 = req.indexOf(' ', sp1 + 1);
            if (sp1 >= 0 && sp2 > sp1)
                path = QString::fromLatin1(req.mid(sp1 + 1, sp2 - sp1 - 1));

            QByteArray body;
            if (path.startsWith("/api/stats")) {
                body = stats_provider_ ? stats_provider_() : "{}";
                c->write("HTTP/1.1 200 OK\r\nContent-Type: "
                         "application/json\r\nContent-Length: " +
                         QByteArray::number(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body);
            } else if (path.startsWith("/api/report")) {
                body = report_text_.toUtf8();
                c->write("HTTP/1.1 200 OK\r\nContent-Type: "
                         "text/plain\r\nContent-Length: " +
                         QByteArray::number(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body);
            } else {
                body = dashboardHtml();
                c->write("HTTP/1.1 200 OK\r\nContent-Type: "
                         "text/html; charset=utf-8\r\nContent-Length: " +
                         QByteArray::number(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body);
            }
            c->disconnectFromHost();
        };
        QObject::connect(dash_server_, &QTcpServer::newConnection, this,
            [this, handle] {
                while (QTcpSocket* c = dash_server_->nextPendingConnection()) {
                    // no receiver context: handlers live/die with the socket
                    QObject::connect(c, &QTcpSocket::readyRead,
                        [handle, c] { handle(c); });
                    // request may already be buffered before connect
                    if (c->bytesAvailable() > 0) handle(c);
                    QObject::connect(c, &QTcpSocket::disconnected,
                                     c, &QTcpSocket::deleteLater);
                }
            });
    }
    if (!dash_server_->listen(QHostAddress::Any, port)) return false;
    dash_port_ = port;
    emit dashboardStateChanged(true, dashboardUrl());
    return true;
}

void OnlineServices::stopDashboard() {
    if (dash_server_ && dash_server_->isListening()) {
        dash_server_->close();
        emit dashboardStateChanged(false, {});
    }
}

bool OnlineServices::dashboardRunning() const {
    return dash_server_ && dash_server_->isListening();
}

QString OnlineServices::dashboardUrl() const {
    if (!dashboardRunning()) return {};
    // best-effort LAN IP from interface list (no blocking DNS here -
    // QHostInfo::fromName on the GUI thread can break event dispatch)
    QString ip;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& ni : ifaces) {
        if (!(ni.flags() & QNetworkInterface::IsUp) ||
            (ni.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry& e : ni.addressEntries()) {
            QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                ip = a.toString();
                break;
            }
        }
        if (!ip.isEmpty()) break;
    }
    if (ip.isEmpty()) ip = "127.0.0.1";
    return QString("http://%1:%2/").arg(ip).arg(dash_port_);
}

} // namespace krad

namespace krad {

QByteArray OnlineServices::dashboardHtml() {
    static const char* html = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>KradDeviceInfo — Live</title>
<style>
:root{--bg:#0d1117;--p:#161b22;--l:#28324a;--t:#e6edf3;--d:#8b949e;--a:#4fc3f7}
*{box-sizing:border-box;margin:0}
body{background:var(--bg);color:var(--t);font:15px/1.5 'Segoe UI',system-ui,sans-serif;padding:20px;max-width:640px;margin:auto}
h1{font-size:20px;margin-bottom:4px}
h1 span{color:var(--a)}
.sub{color:var(--d);font-size:12px;margin-bottom:18px}
.gauge{background:var(--p);border:1px solid var(--l);border-radius:12px;padding:14px 16px;margin-bottom:10px}
.gauge .row{display:flex;justify-content:space-between;font-size:13px;color:var(--d);margin-bottom:6px}
.gauge .row b{color:var(--t);font-size:15px}
.bar{height:10px;background:#232d42;border-radius:6px;overflow:hidden}
.bar i{display:block;height:100%;border-radius:6px;background:linear-gradient(90deg,#2563eb,#4fc3f7);width:0%;transition:width .5s}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.mini{background:var(--p);border:1px solid var(--l);border-radius:12px;padding:12px 14px}
.mini .k{color:var(--d);font-size:11.5px;text-transform:uppercase;letter-spacing:.06em}
.mini .v{font-size:19px;font-weight:700;margin-top:3px}
.mini .v small{font-size:11px;color:var(--d);font-weight:400}
footer{color:var(--d);font-size:11px;text-align:center;margin-top:16px}
</style></head><body>
<h1>Krad<span>DeviceInfo</span> — Live</h1>
<div class="sub" id="host">connecting…</div>
<div class="gauge"><div class="row"><span>CPU</span><b id="cpuV">0%</b></div><div class="bar"><i id="cpuB"></i></div></div>
<div class="gauge"><div class="row"><span>Memory</span><b id="ramV">0%</b></div><div class="bar"><i id="ramB"></i></div></div>
<div class="gauge"><div class="row"><span>Disk activity</span><b id="dskV">0%</b></div><div class="bar"><i id="dskB"></i></div></div>
<div class="grid">
<div class="mini"><div class="k">GPU</div><div class="v" id="gpuV">–</div></div>
<div class="mini"><div class="k">Uptime</div><div class="v" id="upV" style="font-size:14px">–</div></div>
<div class="mini"><div class="k">Net ↓ / ↑</div><div class="v" id="netV" style="font-size:14px">–</div></div>
<div class="mini"><div class="k">Disk R / W</div><div class="v" id="dioV" style="font-size:14px">–</div></div>
</div>
<footer>krad.device.info · refreshes every second</footer>
<script>
function fmt(n,u){return n>=1024?(n/1024).toFixed(1)+u[1]:n.toFixed(1)+u[0]}
async function poll(){
 try{
  const r=await fetch('/api/stats');const d=await r.json();
  document.getElementById('host').textContent=d.hostname+' · '+d.os+' · '+d.cores+' cores';
  const set=(id,v)=>{document.getElementById(id+'V').textContent=Math.round(v)+'%';
                     document.getElementById(id+'B').style.width=Math.min(100,v)+'%';
                     const b=document.getElementById(id+'B');
                     b.style.background=v>80?'linear-gradient(90deg,#ef4444,#f97316)':
                                        v>55?'linear-gradient(90deg,#f59e0b,#fbbf24)':
                                        'linear-gradient(90deg,#2563eb,#4fc3f7)';};
  set('cpu',d.cpu);set('ram',d.ram);set('dsk',d.disk_active);
  document.getElementById('gpuV').innerHTML=d.gpu>=0?Math.round(d.gpu)+'<small> %</small>':'n/a';
  document.getElementById('upV').textContent=d.uptime;
  document.getElementById('netV').innerHTML=fmt(d.net_rx,[' KB/s',' MB/s'])+' / '+fmt(d.net_tx,[' KB/s',' MB/s']);
  document.getElementById('dioV').innerHTML=fmt(d.disk_read,[' MB/s',' GB/s'])+' / '+fmt(d.disk_write,[' MB/s',' GB/s']);
 }catch(e){}
}
poll();setInterval(poll,1000);
</script></body></html>)HTML";
    return QByteArray(html);
}

} // namespace krad

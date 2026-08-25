// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - online services: IP geo, speed test, DNS benchmark,
// NTP clock check, update check, report sharing, LAN web dashboard.
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QString>
#include <functional>
#include <vector>

namespace krad {

struct IpGeoInfo {
    QString ip, city, region, country, countryCode;
    QString isp, org, asn, timezone;
    bool ok = false;
    QString error;
};

struct SpeedResult {
    double down_mbps = 0, up_mbps = 0, ping_ms = 0;
    bool ok = false;
    QString error;
};

struct DnsServerResult {
    QString server;
    QString name;
    double avg_ms = 0;
    double min_ms = 0;
    int ok_count = 0;
    int total = 0;
};

class OnlineServices : public QObject {
    Q_OBJECT
public:
    explicit OnlineServices(QObject* parent = nullptr);

    void    fetchIpInfo();
    void    runSpeedTest();
    void    runDnsBenchmark();
    void    fetchNtpOffset();
    void    checkForUpdates(const QString& currentVersion);
    void    shareReport(const QString& text);

    bool    startDashboard(quint16 port);
    void    stopDashboard();
    bool    dashboardRunning() const;
    QString dashboardUrl() const;
    static QByteArray dashboardHtml();

    void    setStatsProvider(std::function<QByteArray()> provider);
    void    setReportText(const QString& text);

    static constexpr const char* VERSION_URL =
        "https://kraddeviceinfo.pages.dev/version.json";

signals:
    void ipInfoReady(const krad::IpGeoInfo& info);
    void speedStage(const QString& stage);
    void speedProgress(double liveMbps);
    void speedDone(const krad::SpeedResult& result);
    void dnsProgress(int serverIdx, int totalServers);
    void dnsDone(const std::vector<krad::DnsServerResult>& results);
    void ntpDone(bool ok, double offsetMs, const QString& server);
    void updateCheckDone(bool newer, const QString& latest,
                         const QString& notes, const QString& error);
    void reportShared(bool ok, const QString& urlOrError);
    void dashboardStateChanged(bool running, const QString& url);

private:
    // speed test internals
    void speedPingRound();
    void speedDownloadRound();
    void speedUploadRound();
    void speedFinish();

    // dns internals
    void dnsNextServer();
    void dnsSendQuery();

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkAccessManager* nam();
    QString report_text_;
    std::function<QByteArray()> stats_provider_;

    // speed state
    enum SpeedPhase { Idle, Pinging, Downloading, Uploading } speed_phase_ = Idle;
    int  ping_rounds_left_ = 0;
    double ping_sum_ms_ = 0; int ping_count_ = 0;
    qint64 dl_bytes_ = 0; qint64 dl_start_ = 0; double down_mbps_ = 0;
    qint64 up_bytes_ = 0; qint64 up_start_ = 0; double up_mbps_ = 0;
    SpeedResult speed_result_;

    // dns state
    std::vector<std::pair<QString, QString>> dns_servers_;  // ip, name
    int dns_idx_ = 0, dns_queries_left_ = 0;
    double dns_sum_ = 0, dns_min_ = 1e9; int dns_ok_ = 0, dns_total_ = 0;
    std::vector<DnsServerResult> dns_results_;
    QUdpSocket* dns_sock_ = nullptr;
    QElapsedTimer dns_timer_;
    quint16 dns_txid_ = 0;

    // dashboard
    QTcpServer* dash_server_ = nullptr;
    quint16 dash_port_ = 8787;
};

} // namespace krad

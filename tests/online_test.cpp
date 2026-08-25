// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// Online services test — single top-level event loop, fully async stages.
// Dashboard server is verified with an external curl process and runs LAST
// because serving in-process requests while QNAM is busy is the most
// interaction-heavy scenario.
#include "../src/app/online.h"

#include <QCoreApplication>
#include <QTimer>
#include <QFile>
#include <cstdlib>
#include <cstdio>

using namespace krad;

static QCoreApplication* g_app = nullptr;
static OnlineServices* g_online = nullptr;
static int failures = 0;
static int stage = 0;
static bool stageDone = false;

static void nextStage();

// plain connect; duplicate emissions are harmless because advance()
// is guarded by stageDone

static void advance() {
    if (stageDone) return;
    stageDone = true;
    ++stage;
    nextStage();
}

static void stageTimeout(const char* name, int fail) {
    if (stageDone) return;
    std::printf("%s stage timeout\n", name);
    failures += fail;
    advance();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    g_app = &app;
    g_online = new OnlineServices(&app);
    advance();          // stage 0 -> 1 (ip info)
    QTimer::singleShot(240000, &app, [&app] {
        std::printf("global timeout\n");
        app.exit(1);
    });
    return app.exec();
}

static void nextStage() {
    stageDone = false;
    QCoreApplication& app = *g_app;
    OnlineServices& online = *g_online;

    if (stage == 1) {
        // ---------------- ip info ----------------
        std::printf("[stage 1] public ip...\n");
        QObject::connect(&online, &OnlineServices::ipInfoReady,
            [&](const IpGeoInfo& i) {
                if (i.ok)
                    std::printf("ip: %s (%s, %s) isp=%s\n",
                                i.ip.toUtf8().constData(),
                                i.city.toUtf8().constData(),
                                i.country.toUtf8().constData(),
                                i.isp.toUtf8().constData());
                else {
                    std::printf("ip lookup FAIL: %s\n",
                                i.error.toUtf8().constData());
                    ++failures;
                }
                advance();
            });
        online.fetchIpInfo();
        QTimer::singleShot(20000, &app, [] { stageTimeout("ip", 1); });
    } else if (stage == 2) {
        // ---------------- speed test ----------------
        std::printf("[stage 2] speed test...\n");
        QObject::connect(&online, &OnlineServices::speedDone,
            [&](const SpeedResult& r) {
                std::printf("speed: down=%.1f up=%.1f Mbps ping=%.0f ms %s\n",
                            r.down_mbps, r.up_mbps, r.ping_ms,
                            r.ok ? "OK" : "FAIL");
                if (!r.ok || r.down_mbps <= 0) ++failures;
                advance();
            });
        online.runSpeedTest();
        QTimer::singleShot(90000, &app, [] { stageTimeout("speed", 1); });
    } else if (stage == 3) {
        // ---------------- dns benchmark ----------------
        std::printf("[stage 3] dns benchmark...\n");
        QObject::connect(&online, &OnlineServices::dnsDone,
            [&](const std::vector<DnsServerResult>& rs) {
                for (auto& r : rs)
                    std::printf("dns %s (%s): avg=%.1f best=%.1f ms %d/%d\n",
                                r.server.toUtf8().constData(),
                                r.name.toUtf8().constData(),
                                r.avg_ms, r.min_ms, r.ok_count, r.total);
                // UDP may be blocked in sandboxed CI - warn only
                if (rs.empty()) std::printf("dns: no results (UDP blocked?)\n");
                advance();
            });
        online.runDnsBenchmark();
        QTimer::singleShot(60000, &app, [] { stageTimeout("dns", 0); });
    } else if (stage == 4) {
        // ---------------- ntp ----------------
        std::printf("[stage 4] ntp...\n");
        QObject::connect(&online, &OnlineServices::ntpDone,
            [&](bool ok, double off, const QString& s) {
                std::printf("ntp: %s offset=%.1f ms (%s)\n",
                            ok ? "OK" : "WARN", off, s.toUtf8().constData());
                advance();
            });
        online.fetchNtpOffset();
        QTimer::singleShot(20000, &app, [] { stageTimeout("ntp", 0); });
    } else if (stage == 5) {
        // ---------------- dashboard server (external curl) ---------------
        std::printf("[stage 5] web dashboard...\n");
        online.setStatsProvider([] {
            return QByteArray("{\"cpu\":42.5,\"ram\":61.0,\"gpu\":-1,"
                              "\"disk_active\":3.0,\"disk_read\":12.5,"
                              "\"disk_write\":4.0,\"net_rx\":120.0,"
                              "\"net_tx\":30.0,\"cores\":6,"
                              "\"hostname\":\"test-pc\",\"os\":\"TestOS\","
                              "\"uptime\":\"1h 2m\"}");
        });
        if (!online.startDashboard(18991)) {
            std::printf("FAIL: dashboard start\n");
            ++failures;
            advance();
            return;
        }
        std::printf("dashboard url: %s\n",
                    online.dashboardUrl().toUtf8().constData());

        // verify with detached curl writing to files (no QProcess object
        // lifecycle issues; server runs in this process, client is external)
        QTimer::singleShot(300, [] {
            std::system("curl -s -m 5 http://127.0.0.1:18991/api/stats "
                        "-o /tmp/opencode/dash_api.txt &");
        });
        QTimer::singleShot(1200, [] {
            std::system("curl -s -m 5 http://127.0.0.1:18991/ "
                        "-o /tmp/opencode/dash_html.txt &");
        });
        QTimer::singleShot(2500, [&] {
            QFile api("/tmp/opencode/dash_api.txt");
            QFile html("/tmp/opencode/dash_html.txt");
            api.open(QIODevice::ReadOnly);
            html.open(QIODevice::ReadOnly);
            QByteArray body = api.readAll();
            QByteArray doc = html.readAll();
            bool apiOk = body.contains("\"cpu\":42.5");
            std::printf("api/stats: %s -> %s\n", apiOk ? "OK" : "FAIL",
                        body.left(60).constData());
            bool htmlOk = doc.contains("Krad") && doc.contains("poll");
            std::printf("dashboard html: %s (%d bytes)\n",
                        htmlOk ? "OK" : "FAIL", int(doc.size()));
            if (!apiOk) ++failures;
            if (!htmlOk) ++failures;
            online.stopDashboard();
            advance();
        });
        QTimer::singleShot(15000, &app, [] { stageTimeout("dashboard", 1); });
    } else {
        std::printf("\n=== %s (failures: %d) ===\n",
                    failures ? "SOME TESTS FAILED" : "ALL TESTS PASS",
                    failures);
        app.exit(failures ? 1 : 0);
    }
}

// GUI online page test: clicks Refresh + Start speed test, verifies labels.
// Uses file-scope state only - no lambda captures of stack locals, so no
// dangling-reference failure modes.
#include "../src/gui/mainwindow.h"
#include "../src/gui/theme.h"

#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <cstdio>

using krad::ui::MainWindow;

static QCoreApplication* g_app = nullptr;
static MainWindow*       g_w = nullptr;
static QPushButton*      g_refresh = nullptr;
static QPushButton*      g_speed = nullptr;
static int    g_phase = 0;          // 0 wait-collect, 1 wait-ip, 2 wait-speed
static int    g_waited = 0;
static int    g_failures = 0;

static void finish(int code) {
    if (g_app) g_app->exit(code);
}

static void pollTick() {
    ++g_waited;
    MainWindow& w = *g_w;

    if (g_phase == 0) {
        // initial collect done -> go to Online page
        auto* sb = w.findChild<QListWidget*>();
        if (sb) sb->setCurrentRow(6);
        g_phase = 1;
        g_waited = 0;
        return;
    }
    if (g_phase == 1) {
        if (g_waited == 2) {                    // page shown, find buttons
            for (auto* b : w.findChildren<QPushButton*>()) {
                if (b->text() == "Refresh") g_refresh = b;
                if (b->text().contains("Start speed")) g_speed = b;
            }
            if (!g_refresh || !g_speed) {
                std::printf("FAIL: buttons not found\n");
                finish(1);
                return;
            }
            std::printf("buttons found, clicking Refresh...\n");
            g_refresh->click();
        }
        if (g_waited > 8 && g_refresh) {        // ip lookup had ~6 s
            for (auto* l : w.findChildren<QLabel*>()) {
                if (l->text().startsWith("<b>") && l->text().contains(".")) {
                    std::printf("IP RESULT: %s\n",
                                l->text().remove(QRegExp("<[^>]*>"))
                                    .toUtf8().constData());
                    g_phase = 2;
                    g_waited = 0;
                    if (g_speed) g_speed->click();
                    std::printf("clicked Speed test...\n");
                    return;
                }
            }
        }
        if (g_waited > 30) {
            std::printf("FAIL: ip stage timeout\n");
            finish(1);
        }
        return;
    }
    if (g_phase == 2) {
        for (auto* l : w.findChildren<QLabel*>()) {
            if (l->text().contains("Down ") && l->text().contains("Mbps")) {
                std::printf("SPEED RESULT: %s\n", l->text().toUtf8().constData());
                std::printf("GUI ONLINE TEST PASS\n");
                finish(0);
                return;
            }
        }
        if (g_waited > 60) {
            std::printf("FAIL: speed stage timeout\n");
            finish(1);
        }
        return;
    }
}

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    g_app = &app;
    QApplication::setApplicationName("KradDeviceInfo");
    krad::ui::Theme::instance().apply(krad::ui::ThemeKind::Dark);

    MainWindow w;
    g_w = &w;
    w.show();

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, pollTick);
    timer.start(500);

    QTimer::singleShot(240000, &app, [] {
        std::printf("global timeout\n");
        finish(1);
    });
    return app.exec();
}

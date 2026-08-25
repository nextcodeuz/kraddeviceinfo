// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// GUI benchmark end-to-end test: clicks the CPU bench button, waits for the
// worker to finish, verifies the result label is populated, exits 0/1.
#include "../src/gui/mainwindow.h"
#include "../src/gui/theme.h"

#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <cstdio>

using krad::ui::MainWindow;

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName("KradDeviceInfo");
    krad::ui::Theme::instance().apply(krad::ui::ThemeKind::Dark);

    MainWindow w;
    w.show();

    QTimer::singleShot(3000, [&] {          // let initial collect finish
        QList<QPushButton*> buttons = w.findChildren<QPushButton*>();
        QPushButton* cpu_btn = nullptr;
        for (auto* b : buttons)
            if (b->text().contains("CPU test")) { cpu_btn = b; break; }
        if (!cpu_btn) {
            std::printf("FAIL: cpu bench button not found\n");
            app.exit(1);
            return;
        }
        cpu_btn->click();
        std::printf("clicked CPU benchmark...\n");

        // poll for completion up to 40 s
        int waited = 0;
        auto* poll = new QTimer(&w);
        QObject::connect(poll, &QTimer::timeout, [&] {
            waited += 500;
            QLabel* result = nullptr;
            for (auto* l : w.findChildren<QLabel*>())
                if (l->text().startsWith("CPU: single=")) { result = l; break; }
            if (result) {
                std::printf("RESULT: %s\n", result->text().toUtf8().constData());
                std::printf("GUI BENCH TEST PASS\n");
                app.exit(0);
            } else if (waited > 40000) {
                std::printf("FAIL: timeout waiting for bench result\n");
                app.exit(1);
            }
        });
        poll->start(500);
    });
    return app.exec();
}

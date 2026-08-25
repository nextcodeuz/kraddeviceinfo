// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// Screenshot tool for CI verification of the UI
#include "../src/gui/mainwindow.h"
#include "../src/gui/theme.h"

#include <QApplication>
#include <QTimer>
#include <QPixmap>
#include <QScreen>
#include <QListWidget>
#include <QSettings>
#include <cstdio>

using krad::ui::MainWindow;

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName("KradDeviceInfo");
    QApplication::setOrganizationName("krad.device.info");

    bool dark = true;
    for (int i = 1; i < argc; ++i)
        if (QString(argv[i]) == "--light") dark = false;
    krad::ui::Theme::instance().apply(
        dark ? krad::ui::ThemeKind::Dark : krad::ui::ThemeKind::Light);

    MainWindow w;
    w.show();

    // let async collection finish + a few perf samples accumulate
    QTimer::singleShot(3500, [&] {
        auto* sb = w.findChild<QListWidget*>();
        const QString prefix = argc > 1 ? argv[argc - 1] : "shot";
        for (int page = 0; page < 10; ++page) {
            QTimer::singleShot(250 * page, [&w, sb, page, prefix] {
                if (sb) sb->setCurrentRow(page);
                QTimer::singleShot(120, [&w, page, prefix] {
                    w.grab().save(QString("/tmp/opencode/%1_%2.png")
                                      .arg(prefix).arg(page));
                    if (page == 9) {
                        std::printf("screenshots done\n");
                        QApplication::exit(0);
                    }
                });
            });
        }
    });
    return app.exec();
}

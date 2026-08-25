// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - application entry point
#include "cli.h"
#include "../gui/theme.h"
#include "../gui/mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLockFile>
#include <QStyleFactory>
#include <QSettings>
#include <QDir>
#include <memory>

#include "core/collect.h"

using namespace krad;

// CLI flags handled without starting the GUI event loop
static bool is_cli_invocation(const std::vector<std::string>& args) {
    static const char* flags[] = {
        "--export", "-e", "--output", "-o", "--bench", "--duration", "-d",
        "--drive", "--monitor", "--interval", "--help", "-h",
        "--version", "-V",
    };
    for (auto& a : args)
        for (const char* f : flags)
            if (a == f) return true;
    return false;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);

    // ---- pure CLI path (no GUI libs initialized) ----------------------------
    if (is_cli_invocation(args)) {
        QCoreApplication capp(argc, argv);
        capp.setApplicationName(krad::APP_NAME);
        capp.setOrganizationName(krad::APP_ID);
        int code = cli::run(args);
        return code == 0x7FFFFFFF ? 0 : code;
    }

    // ---- GUI path ------------------------------------------------------------
    QApplication app(argc, argv);
    QApplication::setApplicationName(krad::APP_NAME);
    QApplication::setApplicationDisplayName(krad::APP_NAME);
    QApplication::setOrganizationName(krad::APP_ID);
    QApplication::setApplicationVersion(krad::APP_VERSION);

    collect::perf_init();

    QSettings st;
    const bool dark = st.value("ui/dark", true).toBool();
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    ui::Theme::instance().apply(dark ? ui::ThemeKind::Dark
                                     : ui::ThemeKind::Light);

    // single instance guard
    QLockFile lock(QDir::tempPath() + "/" + APP_ID + ".lock");
    lock.setStaleLockTime(0);

    int rc;
    if (lock.tryLock(100)) {
        ui::MainWindow w;
        w.show();
        rc = app.exec();
    } else {
        rc = 0;   // another instance already running
    }
    collect::perf_shutdown();
    return rc;
}

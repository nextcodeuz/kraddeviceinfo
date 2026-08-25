// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - main window with sidebar navigation
#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QAtomicInt>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <vector>

#include <krad/model.h>

namespace krad { class OnlineServices; }

class QListWidget;
class QStackedWidget;
class QLabel;
class QLineEdit;

namespace krad {
namespace ui {

class Gauge;
class RealtimeChart;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected slots:
    void onRefreshClicked();
    void onExportAs(QAction* action);
    void onThemeToggle();
    void onPageChanged(int index);
    void tick();                       // 1s monitoring sample
    void applyReport(const krad::DeviceReport& r);

private:
    void buildUi();
    void buildSidebar();
    void buildPages();
    void buildMenu();
    void startAsyncCollect();
    void runBenchmark(int kind);        // 0 cpu, 1 memory, 2 disk
    void setBenchRunning(bool running);
    void connectOnlineSignals();
    QByteArray dashboardStatsJson();

    // data holders shared with pages
    krad::DeviceReport report_;

    QListWidget* sidebar_ = nullptr;
    OnlineServices* online_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QTimer perf_timer_;
    std::int64_t last_refresh_sec_ = 0;

    // benchmark state
    QAtomicInt bench_cancel_{0};
    QWidget* bench_page_ = nullptr;
    QLabel* bench_status_ = nullptr;
    QProgressBar* bench_progress_ = nullptr;
    QTreeWidget* bench_results_ = nullptr;
    std::vector<QPushButton*> bench_buttons_;
};

} // namespace ui
} // namespace krad

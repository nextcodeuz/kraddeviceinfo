// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - main window implementation (all pages)
#include "mainwindow.h"
#include "theme.h"
#include "widgets/gauge.h"
#include "widgets/chart.h"
#include "widgets/pageparts.h"
#include "icons.h"
#include "../core/collect.h"
#include "../core/util.h"
#include "../app/export.h"
#include "../app/online.h"
#include "../core/bench.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QtConcurrent>
#include <QMenu>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QProgressBar>
#include <QToolBar>
#include <QToolButton>
#include <QSpinBox>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <QGroupBox>
#include <QApplication>
#include <QStyle>
#include <chrono>
#include <array>

namespace krad {
namespace ui {

// ---------------------------------------------------------------- helpers
static QWidget* makeScrollChild(QWidget* w) {
    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(w);
    return sa;
}

static QLabel* kv(const QString& key) {
    auto* l = new QLabel(key);
    l->setStyleSheet("color: palette(placeholder-text);");
    return l;
}

// ---------------------------------------------------------------- build
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("%1 v%2").arg(krad::APP_NAME, krad::APP_VERSION));
    resize(1180, 760);
    setWindowIcon(makeAppIcon());

    online_ = new krad::OnlineServices(this);
    online_->setStatsProvider([this] { return dashboardStatsJson(); });
    buildUi();
    buildPages();
    connectOnlineSignals();
    buildMenu();

    statusBar()->showMessage(tr("Collecting system information..."));

    // perf timer
    connect(&perf_timer_, &QTimer::timeout, this, &MainWindow::tick);
    perf_timer_.start(1000);
    QTimer::singleShot(50, this, &MainWindow::tick);

    startAsyncCollect();
}

void MainWindow::buildUi() {
    auto* central = new QWidget();
    auto* h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    sidebar_ = new QListWidget();
    sidebar_->setObjectName("sidebar");
    sidebar_->setIconSize(QSize(20, 20));
    sidebar_->setFixedWidth(190);

    stack_ = new QStackedWidget();

    h->addWidget(sidebar_);
    h->addWidget(stack_, 1);
    setCentralWidget(central);

    connect(sidebar_, &QListWidget::currentRowChanged,
            this, &MainWindow::onPageChanged);
}

void MainWindow::buildSidebar() {
    struct Entry { const char* title; Icon id; };
    static const Entry entries[] = {
        {"Overview",   Icon::Overview},
        {"CPU",        Icon::Cpu},
        {"Memory",     Icon::Memory},
        {"GPU",        Icon::Gpu},
        {"Storage",    Icon::Disk},
        {"Network",    Icon::Network},
        {"Online",     Icon::Globe},
        {"Devices",    Icon::Monitor},
        {"Software",   Icon::Software},
        {"Benchmark",  Icon::Bench},
        {"Report",     Icon::Report},
    };
    for (auto& e : entries)
        sidebar_->addItem(new QListWidgetItem(Icons::get(e.id), e.title));
    sidebar_->setCurrentRow(0);
}

void MainWindow::buildPages() {
    // ============ Overview ============
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 18, 22, 12);

        auto* row1 = new QHBoxLayout();
        row1->setSpacing(14);
        Gauge* g_cpu = new Gauge(); g_cpu->setLabel("CPU"); g_cpu->setSubLabel("Processor load");
        Gauge* g_ram = new Gauge(); g_ram->setLabel("RAM"); g_ram->setSubLabel("Memory usage");
        Gauge* g_gpu = new Gauge(); g_gpu->setLabel("GPU"); g_gpu->setSubLabel("GPU utilization");
        Gauge* g_dsk = new Gauge(); g_dsk->setLabel("DISK"); g_dsk->setSubLabel("Active time");
        for (auto* g : {g_cpu, g_ram, g_gpu, g_dsk}) {
            g->setMinimumSize(150, 150);
            row1->addWidget(g, 1, Qt::AlignHCenter);
        }
        v->addLayout(row1);

        auto* row2 = new QHBoxLayout();
        auto* chart_cpu = new RealtimeChart();
        chart_cpu->addSeries("CPU %", QColor("#4fc3f7"), 100);
        chart_cpu->setTitle("CPU history");
        chart_cpu->setMaxPoints(90);
        auto* chart_ram = new RealtimeChart();
        chart_ram->addSeries("RAM %", QColor("#a78bfa"), 100);
        chart_ram->setTitle("Memory history");
        chart_ram->setMaxPoints(90);
        row2->addWidget(chart_cpu, 1);
        row2->addWidget(chart_ram, 1);
        v->addLayout(row2, 2);

        auto* specs = new SectionView();
        v->addWidget(makeScrollChild(specs), 3);

        page->setProperty("g_cpu", QVariant::fromValue(static_cast<QWidget*>(g_cpu)));
        page->setProperty("g_ram", QVariant::fromValue(static_cast<QWidget*>(g_ram)));
        page->setProperty("g_gpu", QVariant::fromValue(static_cast<QWidget*>(g_gpu)));
        page->setProperty("g_dsk", QVariant::fromValue(static_cast<QWidget*>(g_dsk)));
        page->setProperty("chart_cpu", QVariant::fromValue(static_cast<QWidget*>(chart_cpu)));
        page->setProperty("chart_ram", QVariant::fromValue(static_cast<QWidget*>(chart_ram)));
        page->setProperty("specs", QVariant::fromValue(static_cast<QWidget*>(specs)));

        stack_->addWidget(page);
    }

    // CPU page: details + per-core live chart
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(8);

        auto* head = new QHBoxLayout();
        head->addWidget(new PageTitle("CPU"));
        head->addStretch();
        auto* search = new QLineEdit();
        search->setPlaceholderText(tr("Search..."));
        search->setFixedWidth(240);
        head->addWidget(search);
        v->addLayout(head);

        auto* split = new QHBoxLayout();
        auto* tree = new KVTree();
        auto* chart = new RealtimeChart();
        chart->addSeries("core %", QColor("#4fc3f7"), 100);
        chart->setTitle("Per-core load");
        split->addWidget(tree, 3);
        split->addWidget(chart, 2);
        v->addLayout(split, 1);
        connect(search, &QLineEdit::textChanged, tree, &KVTree::setFilter);

        page->setProperty("kvtree", QVariant::fromValue(static_cast<QWidget*>(tree)));
        page->setProperty("chart", QVariant::fromValue(static_cast<QWidget*>(chart)));
        stack_->addWidget(page);
    }

    // Memory page: tree + live chart
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(8);
        v->addWidget(new PageTitle("Memory"));
        auto* split = new QHBoxLayout();
        auto* tree = new KVTree();
        auto* chart = new RealtimeChart();
        chart->addSeries("Used %", QColor("#a78bfa"), 100);
        chart->setTitle("Usage history");
        split->addWidget(tree, 3);
        split->addWidget(chart, 2);
        v->addLayout(split, 1);
        page->setProperty("kvtree", QVariant::fromValue(static_cast<QWidget*>(tree)));
        page->setProperty("chart", QVariant::fromValue(static_cast<QWidget*>(chart)));
        stack_->addWidget(page);
    }

    // GPU page: per-adapter cards via SectionView + live chart
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(8);
        v->addWidget(new PageTitle("GPU"));
        auto* split = new QHBoxLayout();
        auto* sections = new SectionView();
        auto* chart = new RealtimeChart();
        chart->addSeries("GPU %", QColor("#f472b6"), 100);
        chart->setTitle("Utilization");
        auto* sa = qobject_cast<QScrollArea*>(makeScrollChild(sections));
        split->addWidget(sa, 3);
        split->addWidget(chart, 2);
        v->addLayout(split, 1);
        page->setProperty("sections", QVariant::fromValue(static_cast<QWidget*>(sections)));
        page->setProperty("chart", QVariant::fromValue(static_cast<QWidget*>(chart)));
        stack_->addWidget(page);
    }

    // Storage / Network pages
    struct SimplePage {
        const char* name;
        bool chart;
        std::vector<std::pair<QString, QColor>> series;
        QString chart_title;
    };
    const SimplePage simple[] = {
        {"Storage", true,  {{"read MB/s", QColor("#34d399")},
                            {"write MB/s", QColor("#fbbf24")}}, "Disk throughput"},
        {"Network", true,  {{"RX KB/s", QColor("#60a5fa")},
                            {"TX KB/s", QColor("#f472b6")}}, "Network throughput"},
    };
    for (auto& s : simple) {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(8);

        auto* head = new QHBoxLayout();
        head->addWidget(new PageTitle(s.name));
        head->addStretch();
        auto* search = new QLineEdit();
        search->setPlaceholderText(tr("Search..."));
        search->setFixedWidth(240);
        head->addWidget(search);
        v->addLayout(head);

        if (s.chart) {
            auto* split = new QHBoxLayout();
            auto* tree = new KVTree();
            auto* chart = new RealtimeChart();
            for (auto& [n, c] : s.series) chart->addSeries(n, c, 100);
            chart->setTitle(s.chart_title);
            split->addWidget(tree, 3);
            split->addWidget(chart, 2);
            v->addLayout(split, 1);
            page->setProperty("chart",
                QVariant::fromValue(static_cast<QWidget*>(chart)));
            page->setProperty("kvtree",
                QVariant::fromValue(static_cast<QWidget*>(tree)));
            connect(search, &QLineEdit::textChanged,
                    tree, &KVTree::setFilter);
        } else {
            auto* tree = new KVTree();
            v->addWidget(tree, 1);
            page->setProperty("kvtree",
                QVariant::fromValue(static_cast<QWidget*>(tree)));
            connect(search, &QLineEdit::textChanged,
                    tree, &KVTree::setFilter);
        }
        stack_->addWidget(page);
    }

    // Online page: internet tools
    {
        auto* page = new QWidget();
        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(22, 14, 22, 10);
        outer->addWidget(new PageTitle("Online"));

        auto* sa = new QScrollArea();
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget();
        auto* grid = new QGridLayout(body);
        grid->setSpacing(12);
        int R = 0;

        // ---- Public IP ----
        {
            auto* g = new QGroupBox(tr("Public IP & location"));
            auto* v = new QVBoxLayout(g);
            auto* info = new QLabel(tr("Press refresh to look up your public IP."));
            info->setWordWrap(true);
            auto* btn = new QPushButton(tr("Refresh"));
            v->addWidget(info, 1);
            v->addWidget(btn, 0, Qt::AlignRight);
            grid->addWidget(g, R, 0);
            page->setProperty("ipinfo", QVariant::fromValue(static_cast<QWidget*>(info)));
            connect(btn, &QPushButton::clicked, this, [this] {
                online_->fetchIpInfo();
            });
        }
        // ---- Speed test ----
        {
            auto* g = new QGroupBox(tr("Speed test (Cloudflare edge)"));
            auto* v = new QVBoxLayout(g);
            auto* stage = new QLabel(tr("Ready. Test measures ping, download and upload."));
            stage->setWordWrap(true);
            auto* prog = new QProgressBar();
            auto* res = new QLabel("-");
            QFont rf = res->font(); rf.setPixelSize(15); rf.setBold(true);
            res->setFont(rf);
            auto* btn = new QPushButton(tr("Start speed test"));
            v->addWidget(stage);
            v->addWidget(prog);
            v->addWidget(res);
            v->addWidget(btn, 0, Qt::AlignRight);
            grid->addWidget(g, R, 1);
            page->setProperty("speed_btn", QVariant::fromValue(static_cast<QWidget*>(btn)));
            page->setProperty("speed_stage", QVariant::fromValue(static_cast<QWidget*>(stage)));
            page->setProperty("speed_prog", QVariant::fromValue(static_cast<QWidget*>(prog)));
            page->setProperty("speed_res", QVariant::fromValue(static_cast<QWidget*>(res)));
            connect(btn, &QPushButton::clicked, this,
                    [this] { online_->runSpeedTest(); });
        }
        ++R;
        // ---- DNS benchmark ----
        {
            auto* g = new QGroupBox(tr("DNS benchmark"));
            auto* v = new QVBoxLayout(g);
            auto* tree = new QTreeWidget();
            tree->setRootIsDecorated(false);
            tree->setHeaderLabels({tr("Server"), tr("Provider"), tr("Avg ms"), tr("Best ms"), tr("OK")});
            tree->setFixedHeight(150);
            auto* btn = new QPushButton(tr("Run benchmark"));
            v->addWidget(tree, 1);
            v->addWidget(btn, 0, Qt::AlignRight);
            grid->addWidget(g, R, 0);
            page->setProperty("dns_btn", QVariant::fromValue(static_cast<QWidget*>(btn)));
            page->setProperty("dns_tree", QVariant::fromValue(static_cast<QWidget*>(tree)));
            connect(btn, &QPushButton::clicked, this,
                    [this] { online_->runDnsBenchmark(); });
        }
        // ---- NTP + Updates ----
        {
            auto* g = new QGroupBox(tr("Time sync (NTP)"));
            auto* v = new QVBoxLayout(g);
            auto* res = new QLabel(tr("Check your clock against time servers."));
            res->setWordWrap(true);
            auto* btn = new QPushButton(tr("Check clock"));
            v->addWidget(res, 1);
            v->addWidget(btn, 0, Qt::AlignRight);
            grid->addWidget(g, R, 1);
            page->setProperty("ntp_res", QVariant::fromValue(static_cast<QWidget*>(res)));
            connect(btn, &QPushButton::clicked, this,
                    [this] { online_->fetchNtpOffset(); });
        }
        ++R;
        // ---- Updates ----
        {
            auto* g = new QGroupBox(tr("Updates"));
            auto* v = new QVBoxLayout(g);
            auto* res = new QLabel(tr("You are running the latest version check."));
            res->setWordWrap(true);
            auto* row = new QHBoxLayout();
            auto* btn = new QPushButton(tr("Check for updates"));
            auto* open = new QPushButton(tr("Open downloads"));
            row->addWidget(btn); row->addWidget(open); row->addStretch();
            v->addWidget(res, 1);
            v->addLayout(row);
            grid->addWidget(g, R, 0);
            page->setProperty("upd_res", QVariant::fromValue(static_cast<QWidget*>(res)));
            connect(btn, &QPushButton::clicked, this, [this] {
                online_->checkForUpdates(krad::APP_VERSION);
            });
            connect(open, &QPushButton::clicked, this, [] {
                QDesktopServices::openUrl(
                    QUrl("https://kraddeviceinfo.pages.dev"));
            });
        }
        // ---- Share ----
        {
            auto* g = new QGroupBox(tr("Share report online"));
            auto* v = new QVBoxLayout(g);
            auto* res = new QLabel(tr("Upload the full report and get a shareable link."));
            res->setWordWrap(true);
            res->setTextInteractionFlags(Qt::TextSelectableByMouse);
            auto* btn = new QPushButton(tr("Upload report"));
            v->addWidget(res, 1);
            v->addWidget(btn, 0, Qt::AlignRight);
            grid->addWidget(g, R, 1);
            page->setProperty("share_btn", QVariant::fromValue(static_cast<QWidget*>(btn)));
            page->setProperty("share_res", QVariant::fromValue(static_cast<QWidget*>(res)));
            connect(btn, &QPushButton::clicked, this, [this] {
                QString txt = QString::fromStdString(export_::to_txt(report_));
                online_->setReportText(txt);
                online_->shareReport(txt);
            });
        }
        ++R;
        // ---- Web dashboard ----
        {
            auto* g = new QGroupBox(tr("Web dashboard (LAN) — watch this PC from your phone"));
            auto* v = new QVBoxLayout(g);
            auto* res = new QLabel(tr("Starts a tiny read-only HTTP server on your LAN."));
            res->setWordWrap(true);
            auto* row = new QHBoxLayout();
            auto* port = new QSpinBox();
            port->setRange(1024, 65535); port->setValue(8787);
            auto* btn = new QPushButton(tr("Start server"));
            row->addWidget(new QLabel(tr("Port:")));
            row->addWidget(port);
            row->addWidget(btn);
            row->addStretch();
            v->addWidget(res, 1);
            v->addLayout(row);
            grid->addWidget(g, R, 0, 1, 2);
            page->setProperty("dash_res", QVariant::fromValue(static_cast<QWidget*>(res)));
            page->setProperty("dash_btn", QVariant::fromValue(static_cast<QWidget*>(btn)));
            page->setProperty("dash_port", QVariant::fromValue(static_cast<QWidget*>(port)));
            connect(btn, &QPushButton::clicked, this, [this, res, btn, port] {
                if (online_->dashboardRunning()) {
                    online_->stopDashboard();
                    return;
                }
                if (!online_->startDashboard(quint16(port->value()))) {
                    res->setText(tr("Could not bind port %1 — try another.")
                                     .arg(port->value()));
                    return;
                }
            });
        }

        outer->addWidget(sa);
        sa->setWidget(body);
        stack_->addWidget(page);
    }

    // Devices page
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(8);

        auto* head = new QHBoxLayout();
        head->addWidget(new PageTitle("Devices"));
        head->addStretch();
        auto* search = new QLineEdit();
        search->setPlaceholderText(tr("Search..."));
        search->setFixedWidth(240);
        head->addWidget(search);
        v->addLayout(head);

        auto* tree = new KVTree();
        v->addWidget(tree, 1);
        page->setProperty("kvtree", QVariant::fromValue(static_cast<QWidget*>(tree)));
        connect(search, &QLineEdit::textChanged, tree, &KVTree::setFilter);
        stack_->addWidget(page);
    }

    // Software: apps/startup/services tables with filter
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(8);
        auto* head = new QHBoxLayout();
        head->addWidget(new PageTitle("Software"));
        head->addStretch();
        auto* search = new QLineEdit();
        search->setPlaceholderText(tr("Filter..."));
        search->setFixedWidth(260);
        head->addWidget(search);
        v->addLayout(head);

        auto* tree = new QTreeWidget();
        tree->setAlternatingRowColors(true);
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(true);
        tree->setColumnCount(4);
        tree->setHeaderLabels({tr("Name"), tr("Detail"), tr("Info"), tr("Extra")});
        v->addWidget(tree, 1);

        // live filter: hide non-matching rows, keep groups with matches
        connect(search, &QLineEdit::textChanged, tree,
                [tree](const QString& text) {
                    const QString f = text.trimmed();
                    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                        auto* grp = tree->topLevelItem(i);
                        bool any_visible = false;
                        for (int j = 0; j < grp->childCount(); ++j) {
                            auto* c = grp->child(j);
                            bool vis = f.isEmpty();
                            for (int col = 0; col < 4 && !vis; ++col)
                                vis = c->text(col).contains(f, Qt::CaseInsensitive);
                            c->setHidden(!vis);
                            any_visible |= vis;
                        }
                        grp->setHidden(!f.isEmpty() && !any_visible &&
                                       grp->childCount() > 0);
                        grp->setExpanded(true);
                    }
                });

        page->setProperty("softtree", QVariant::fromValue(static_cast<QWidget*>(tree)));
        page->setProperty("search", QVariant::fromValue(static_cast<QWidget*>(search)));
        stack_->addWidget(page);
    }

    // Benchmark
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(10);
        v->addWidget(new PageTitle("Benchmark"));

        auto* btns = new QHBoxLayout();
        auto* bcpu = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),
                                     tr("Run CPU test"));
        auto* bmem = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),
                                     tr("Run memory test"));
        auto* bdsk = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),
                                     tr("Run disk test"));
        auto* stop = new QPushButton(style()->standardIcon(QStyle::SP_BrowserStop),
                                     tr("Stop"));
        stop->setEnabled(false);
        for (auto* b : {bcpu, bmem, bdsk, stop}) btns->addWidget(b);
        btns->addStretch();
        v->addLayout(btns);

        auto* status = new QLabel(tr("Choose a benchmark to run. "
                                     "Results are indicative."));
        status->setWordWrap(true);
        v->addWidget(status);

        auto* progress = new QProgressBar();
        progress->setRange(0, 1000);
        v->addWidget(progress);

        auto* results = new KVTree();
        v->addWidget(results, 1);

        bench_page_ = page;
        bench_status_ = status;
        bench_progress_ = progress;
        bench_results_ = results;
        bench_buttons_ = {bcpu, bmem, bdsk};

        connect(bcpu, &QPushButton::clicked, this, [this] { runBenchmark(0); });
        connect(bmem, &QPushButton::clicked, this, [this] { runBenchmark(1); });
        connect(bdsk, &QPushButton::clicked, this, [this] { runBenchmark(2); });
        connect(stop, &QPushButton::clicked, this, [this] {
            bench_cancel_.storeRelease(1);
            bench_status_->setText(tr("Stopping..."));
        });

        stack_->addWidget(page);
    }

    // Report/About page
    {
        auto* page = new QWidget();
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(22, 14, 22, 10);
        v->setSpacing(12);
        auto* title = new QLabel(krad::APP_NAME);
        QFont f = title->font();
        f.setPixelSize(26); f.setBold(true);
        title->setFont(f);
        v->addWidget(title);
        auto* sub = new QLabel(
            QString("v%1  •  %2\nComplete hardware & software inventory tool.\n"
                    "Native Win32/WMI/SMBIOS backend, no external dependencies.")
                .arg(krad::APP_VERSION, krad::APP_ID));
        sub->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(sub);

        auto* exp = new QGroupBox(tr("Export report"));
        auto* ev = new QVBoxLayout(exp);
        auto* row = new QHBoxLayout();
        auto* bj = new QPushButton("JSON");
        auto* bh = new QPushButton("HTML");
        auto* bt = new QPushButton("TXT");
        auto* bc = new QPushButton("CSV");
        for (auto* b : {bj, bh, bt, bc}) row->addWidget(b);
        row->addStretch();
        ev->addLayout(row);
        v->addWidget(exp);

        auto* info = new KVTree();
        v->addWidget(info, 1);
        page->setProperty("sections", QVariant::fromValue(static_cast<QWidget*>(info)));

        for (auto* b : {bj, bh, bt, bc}) {
            QString fmt = b->text().toLower();
            connect(b, &QPushButton::clicked, this, [this, fmt] {
                QString filter = fmt == "json" ? "JSON (*.json)" :
                                 fmt == "html" ? "HTML (*.html)" :
                                 fmt == "csv"  ? "CSV (*.csv)" : "Text (*.txt)";
                QString path = QFileDialog::getSaveFileName(
                    this, tr("Export report"), "kraddeviceinfo-report." + fmt,
                    filter);
                if (path.isEmpty()) return;
                bool ok = false;
                if      (fmt == "json") ok = export_::write_json(report_, path.toStdString());
                else if (fmt == "html") ok = export_::write_html(report_, path.toStdString());
                else if (fmt == "csv")  ok = export_::write_csv(report_, path.toStdString());
                else                    ok = export_::write_txt(report_, path.toStdString());
                QMessageBox::information(this, krad::APP_NAME,
                    ok ? tr("Report saved to:\n%1").arg(path)
                       : tr("Failed to write file"));
            });
        }

        stack_->addWidget(page);
    }

    buildSidebar();
}

void MainWindow::buildMenu() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QMenu* exp = file->addMenu(style()->standardIcon(QStyle::SP_DialogSaveButton),
                               tr("&Export"));
    exp->addAction("JSON")->setData("json");
    exp->addAction("HTML")->setData("html");
    exp->addAction("TXT")->setData("txt");
    exp->addAction("CSV")->setData("csv");
    connect(exp, &QMenu::triggered, this, &MainWindow::onExportAs);
    file->addAction(style()->standardIcon(QStyle::SP_BrowserReload),
                    tr("&Refresh"))->setShortcut(QKeySequence::Refresh);
    connect(file, &QMenu::triggered, this, [this](QAction* a) {
        if (a->data().isNull()) onRefreshClicked();
    });
    file->addSeparator();
    auto* quit = file->addAction(tr("E&xit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    auto* theme_act = view->addAction(tr("Toggle &theme"));
    connect(theme_act, &QAction::triggered, this, &MainWindow::onThemeToggle);

    // ---- toolbar -------------------------------------------------------------
    auto* tb = addToolBar(tr("Main"));
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* act_refresh = tb->addAction(
        style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh"));
    act_refresh->setShortcut(QKeySequence::Refresh);
    connect(act_refresh, &QAction::triggered,
            this, &MainWindow::onRefreshClicked);

    auto* act_bench = tb->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Benchmark"));
    connect(act_bench, &QAction::triggered, this, [this] {
        sidebar_->setCurrentRow(9);
    });

    tb->addSeparator();
    auto* act_theme = tb->addAction(
        style()->standardIcon(QStyle::SP_DesktopIcon), tr("Theme"));
    connect(act_theme, &QAction::triggered,
            this, &MainWindow::onThemeToggle);

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    auto* act_export = tb->addAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Export"));
    auto* exp_menu = new QMenu(this);
    exp_menu->addAction("JSON")->setData("json");
    exp_menu->addAction("HTML")->setData("html");
    exp_menu->addAction("TXT")->setData("txt");
    exp_menu->addAction("CSV")->setData("csv");
    connect(exp_menu, &QMenu::triggered, this, &MainWindow::onExportAs);
    act_export->setMenu(exp_menu);
}

// ---------------------------------------------------------------- actions
void MainWindow::onPageChanged(int index) {
    if (index >= 0 && index < stack_->count()) stack_->setCurrentIndex(index);
}

void MainWindow::onThemeToggle() {
    QSettings st;
    bool dark = st.value("ui/dark", true).toBool();
    dark = !dark;
    st.setValue("ui/dark", dark);
    Theme::instance().apply(dark ? ThemeKind::Dark : ThemeKind::Light);
}

void MainWindow::startAsyncCollect() {
    auto watcher = new QFutureWatcher<DeviceReport>(this);
    connect(watcher, &QFutureWatcher<DeviceReport>::finished, this,
            [this, watcher] {
                applyReport(watcher->result());
                watcher->deleteLater();
            });
    watcher->setFuture(QtConcurrent::run([] {
        return collect::full_report();
    }));
}

void MainWindow::onRefreshClicked() {
    statusBar()->showMessage(tr("Refreshing..."));
    startAsyncCollect();
}

void MainWindow::applyReport(const DeviceReport& r) {
    report_ = r;
    const std::vector<ReportSection> allSections = r.sections();

    auto pageAt = [&](int i) -> QWidget* { return stack_->widget(i); };

    // ---- overview (page 0) ----
    if (auto* pg = pageAt(0)) {
        auto gauge = [&](const char* prop) -> Gauge* {
            return qobject_cast<Gauge*>(
                pg->property(prop).value<QWidget*>());
        };
        if (auto* g = gauge("g_cpu"))
            g->setValue(r.cpu.load_pct.empty() ? 0 :
                        atof(r.cpu.load_pct.c_str()));
        double ram_pct = r.memory.total_phys ?
            pct_of(double(r.memory.total_phys - r.memory.avail_phys),
                   double(r.memory.total_phys)) : 0;
        if (auto* g = gauge("g_ram")) g->setValue(ram_pct);
        if (auto* g = gauge("g_gpu")) g->setValue(0);
        if (auto* g = gauge("g_dsk")) g->setValue(0);

        std::vector<ReportSection> quick;
        ReportSection sys;
        sys.title = "System summary";
        sys.rows.push_back({"OS", r.os.product_name + " (" +
                                    r.os.build_string + ")"});
        sys.rows.push_back({"CPU", r.cpu.brand});
        sys.rows.push_back({"RAM", format_bytes(r.memory.total_phys) + "  •  " +
                                   std::to_string(r.cpu.cores_logical) + "-thread CPU"});
        if (!r.gpus.empty())
            sys.rows.push_back({"GPU", r.gpus[0].name});
        if (!r.disks.empty())
            sys.rows.push_back({"Disk", r.disks[0].model + "  " +
                                       format_bytes(r.disks[0].size_bytes)});
        if (!r.bios.baseboard_manufacturer.empty() ||
            !r.computer.manufacturer.empty())
            sys.rows.push_back({"Board",
                (r.bios.baseboard_manufacturer.empty() ?
                    r.computer.manufacturer : r.bios.baseboard_manufacturer) +
                " " + r.bios.baseboard_product});
        sys.rows.push_back({"Uptime", format_duration_sec(r.os.uptime_sec)});
        if (r.bios.mode == "UEFI" || !r.bios.secure_boot.empty())
            sys.rows.push_back({"Firmware", r.bios.mode +
                (r.bios.secure_boot.empty() ? "" :
                    " • Secure Boot: " + r.bios.secure_boot)});
        quick.push_back(sys);
        if (auto* sv = qobject_cast<SectionView*>(
                pg->property("specs").value<QWidget*>()))
            sv->setSections(quick);
    }

    // ---- cpu (1), storage (4), network (5), devices (6) ----
    auto fill_tree = [&](int idx, const std::vector<ReportSection>& secs) {
        if (auto* pg = pageAt(idx))
            if (auto* t = qobject_cast<KVTree*>(
                    pg->property("kvtree").value<QWidget*>()))
                t->populate(secs);
    };
    auto pick = [&](const std::string& prefix) {
        std::vector<ReportSection> out;
        for (auto& s : r.sections())
            if (starts_with_ci(s.title, prefix)) out.push_back(s);
        return out;
    };
    {
        std::vector<ReportSection> cpu_secs;
        for (auto& s : r.sections())
            if (s.title == "CPU") cpu_secs.push_back(s);
        fill_tree(1, cpu_secs);
        // per-core series count
        if (auto* pg = pageAt(1))
            if (auto* c = qobject_cast<RealtimeChart*>(
                    pg->property("chart").value<QWidget*>())) {
                c->clearSeries();
                c->ensureSeriesCount(int(r.cpu.cores_logical));
            }
    }
    fill_tree(2, [&] {                                     // Memory page
        std::vector<ReportSection> m;
        for (auto& s : r.sections())
            if (s.title == "Memory") m.push_back(s);
        return m;
    }());
    fill_tree(4, pick("Disk"));                            // Storage
    fill_tree(4, [&] {
        auto v = pick("Disk");
        auto vol = pick("Volumes");
        v.insert(v.end(), vol.begin(), vol.end());
        if (v.empty()) {
            ReportSection none; none.title = "No disks detected";
            v.push_back(none);
        }
        return v;
    }());
    fill_tree(5, [&] {                                     // Network
        auto n = pick("Network");
        if (n.empty()) { ReportSection none;
            none.title = "No adapters detected"; n.push_back(none); }
        return n;
    }());
    fill_tree(7, [&] {                                     // Devices
        std::vector<ReportSection> d = pick("Monitor:");
        auto u = pick("USB");
        d.insert(d.end(), u.begin(), u.end());
        auto b = pick("Battery");
        d.insert(d.end(), b.begin(), b.end());
        auto a = pick("Audio");
        d.insert(d.end(), a.begin(), a.end());
        auto bios_sec = pick("BIOS / Board");
        d.insert(d.begin() + 0, bios_sec.begin(), bios_sec.end());
        if (d.empty()) { ReportSection none;
            none.title = "No devices detected"; d.push_back(none); }
        return d;
    }());

    // ---- gpu (3 handled above order) — note stack order:
    // 0 Overview, 1 CPU, 2 Memory, 3 GPU, 4 Storage, 5 Network,
    // 6 Devices, 7 Software, 8 Benchmark, 9 Report
    {
        if (auto* pg = pageAt(3)) {
            std::vector<ReportSection> gpu_secs;
            for (auto& s : allSections)
                if (starts_with_ci(s.title, "GPU:")) gpu_secs.push_back(s);
            if (gpu_secs.empty()) {
                ReportSection none; none.title = "No discrete adapters found";
                gpu_secs.push_back(none);
            }
            if (auto* sv = qobject_cast<SectionView*>(
                    pg->property("sections").value<QWidget*>()))
                sv->setSections(gpu_secs);
        }
    }

    // ---- software (7) ----
    if (auto* pg = pageAt(8)) {
        if (auto* t = qobject_cast<QTreeWidget*>(
                pg->property("softtree").value<QWidget*>())) {
            t->setUpdatesEnabled(false);
            t->clear();
            auto add_group = [&](const QString& title,
                                 const std::vector<std::array<QString, 4>>& rows) {
                auto* root = new QTreeWidgetItem({title, "", "", ""});
                QFont f = root->font(0); f.setBold(true);
                root->setFont(0, f);
                t->addTopLevelItem(root);
                for (auto& rw : rows)
                    root->addChild(new QTreeWidgetItem(
                        {rw[0], rw[1], rw[2], rw[3]}));
                root->setExpanded(true);
            };
            std::vector<std::array<QString, 4>> apps;
            for (auto& a : r.installed_apps)
                apps.push_back({QString::fromStdString(a.name),
                                QString::fromStdString(a.version),
                                QString::fromStdString(a.publisher),
                                QString::fromStdString(a.size_str)});
            add_group(tr("Installed applications (%1)")
                      .arg(apps.size()), apps);

            std::vector<std::array<QString, 4>> startup;
            for (auto& s : r.startup_entries)
                startup.push_back({QString::fromStdString(s.name),
                                   QString::fromStdString(s.command),
                                   QString::fromStdString(s.location),
                                   {}});
            add_group(tr("Startup (%1)").arg(startup.size()), startup);

            std::vector<std::array<QString, 4>> svc;
            for (auto& s : r.services)
                svc.push_back({QString::fromStdString(s.name),
                               QString::fromStdString(s.display_name),
                               QString::fromStdString(s.state),
                               QString::fromStdString(s.start_mode)});
            add_group(tr("Services (%1)").arg(svc.size()), svc);
            t->setUpdatesEnabled(true);
        }
    }

    // ---- report page (9): full sections ----
    if (auto* pg = pageAt(10)) {
        if (auto* t = qobject_cast<KVTree*>(
                pg->property("sections").value<QWidget*>()))
            t->populate(allSections);
    }

    last_refresh_sec_ = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    statusBar()->showMessage(
        tr("Ready • %1 • collected %2")
            .arg(QString::fromStdString(r.os.product_name),
                 QString::fromStdString(r.generated_at)),
        8000);
}

void MainWindow::onExportAs(QAction* action) {
    const QString fmt = action->data().toString();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export report"), "kraddeviceinfo-report." + fmt,
        fmt.toUpper() + " (*." + fmt + ")");
    if (path.isEmpty()) return;
    bool ok = false;
    if      (fmt == "json") ok = export_::write_json(report_, path.toStdString());
    else if (fmt == "html") ok = export_::write_html(report_, path.toStdString());
    else if (fmt == "csv")  ok = export_::write_csv(report_, path.toStdString());
    else                    ok = export_::write_txt(report_, path.toStdString());
    statusBar()->showMessage(ok ? tr("Exported to %1").arg(path)
                                : tr("Export failed"), 5000);
}

void MainWindow::tick() {
    PerfSample s = collect::perf_sample();

    auto pageAt = [&](int i) { return stack_->widget(i); };
    auto chartOf = [&](QWidget* pg) -> RealtimeChart* {
        return qobject_cast<RealtimeChart*>(
            pg->property("chart").value<QWidget*>());
    };

    // gauges + overview charts
    if (auto* pg = pageAt(0)) {
        if (auto* g = qobject_cast<Gauge*>(
                pg->property("g_cpu").value<QWidget*>())) g->setValue(s.cpu_total);
        if (auto* g = qobject_cast<Gauge*>(
                pg->property("g_ram").value<QWidget*>())) g->setValue(s.ram_pct);
        if (auto* g = qobject_cast<Gauge*>(
                pg->property("g_gpu").value<QWidget*>()))
            g->setValue(s.gpu_pct >= 0 ? s.gpu_pct : 0);
        if (auto* g = qobject_cast<Gauge*>(
                pg->property("g_dsk").value<QWidget*>()))
            g->setValue(s.disk_active);
        if (auto* c = qobject_cast<RealtimeChart*>(
                pg->property("chart_cpu").value<QWidget*>()))
            c->pushValues({s.cpu_total});
        if (auto* c = qobject_cast<RealtimeChart*>(
                pg->property("chart_ram").value<QWidget*>()))
            c->pushValues({s.ram_pct});
    }
    // memory (2) / gpu (3)
    if (auto* pg = pageAt(2))
        if (auto* c = chartOf(pg)) c->pushValues({s.ram_pct});
    if (auto* pg = pageAt(3))
        if (auto* c = chartOf(pg)) c->pushValues({s.gpu_pct >= 0 ? s.gpu_pct : 0});

    // cpu per-core (1)
    if (auto* pg = pageAt(1))
        if (auto* c = chartOf(pg)) {
            if (!s.cpu_cores.empty())
                c->pushValues(s.cpu_cores);
            else
                c->pushValues({s.cpu_total});
        }

    // storage (4): disk throughput
    if (auto* pg = pageAt(4))
        if (auto* c = chartOf(pg))
            c->pushValues({s.disk_read_mbs, s.disk_write_mbs});

    // network (5): throughput
    if (auto* pg = pageAt(5))
        if (auto* c = chartOf(pg))
            c->pushValues({s.net_rx_kbps, s.net_tx_kbps});
}


void MainWindow::setBenchRunning(bool running) {
    for (auto* b : bench_buttons_) b->setEnabled(!running);
    if (auto* stop = bench_page_ ?
            qobject_cast<QPushButton*>(
                bench_page_->property("b_stop").value<QWidget*>()) : nullptr)
        stop->setEnabled(running);
    bench_progress_->setValue(0);
}

void MainWindow::runBenchmark(int kind) {
    setBenchRunning(true);
    bench_cancel_.storeRelease(0);
    bench_results_->clear();

    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher] {
                setBenchRunning(false);
                bench_status_->setText(watcher->result());
                watcher->deleteLater();
            });

    auto progress_cb = [this](const bench::Progress& pr) {
        QMetaObject::invokeMethod(this, [this, pr] {
            if (bench_progress_) {
                bench_progress_->setValue(int(pr.fraction * 1000));
                bench_status_->setText(QString("%1 — %2%")
                    .arg(QString::fromStdString(pr.stage))
                    .arg(int(pr.fraction * 100)));
            }
        }, Qt::QueuedConnection);
    };
    auto cancel_cb = [this]() -> bool {
        return bench_cancel_.loadAcquire() != 0;
    };

    const int seconds = 8;
    switch (kind) {
    case 0:
        bench_status_->setText(tr("Running CPU benchmark..."));
        watcher->setFuture(QtConcurrent::run([seconds, progress_cb, cancel_cb] {
            auto r = bench::run_cpu(seconds, progress_cb, cancel_cb);
            return QString("CPU: single=%1  multi=%2  (%3 threads, x%4 speedup)")
                .arg(r.single_score, 0, 'f', 1)
                .arg(r.multi_score, 0, 'f', 1)
                .arg(r.threads)
                .arg(r.single_score ? r.multi_score / r.single_score : 0.0,
                     0, 'f', 2);
        }));
        break;
    case 1:
        bench_status_->setText(tr("Running memory benchmark..."));
        watcher->setFuture(QtConcurrent::run([seconds, progress_cb, cancel_cb] {
            auto r = bench::run_mem(seconds, progress_cb, cancel_cb);
            return QString("Memory: copy=%1 GB/s  read=%2 GB/s  "
                           "write=%3 GB/s  latency=%4 ns")
                .arg(r.copy_gbs, 0, 'f', 1).arg(r.read_gbs, 0, 'f', 1)
                .arg(r.write_gbs, 0, 'f', 1).arg(r.latency_ns, 0, 'f', 0);
        }));
        break;
    case 2: {
        bench_status_->setText(tr("Running disk benchmark..."));
        QString dir = QDir::tempPath();
        watcher->setFuture(QtConcurrent::run(
            [dir, seconds, progress_cb, cancel_cb] {
                auto r = bench::run_disk(dir.toStdString(), seconds,
                                         progress_cb, cancel_cb);
                QString out;
                for (auto& it : r.items)
                    out += QString("%1: %2 MB/s (%3 IOPS)\n")
                        .arg(QString::fromStdString(it.label),
                             QString::number(it.mbps, 'f', 1),
                             QString::number(it.iops, 'f', 0));
                return out.isEmpty() ? tr("Disk benchmark cancelled") : out;
            }));
        break;
    }
    }
}

void MainWindow::connectOnlineSignals() {
    auto pageAt = [&](int i) { return stack_->widget(i); };
    QWidget* pg = pageAt(6);
    if (!pg) return;
    auto w = [&](const char* prop) -> QWidget* {
        return pg->property(prop).value<QWidget*>();
    };

    connect(online_, &krad::OnlineServices::ipInfoReady, this,
            [this, pg](const krad::IpGeoInfo& i) {
        auto* info = qobject_cast<QLabel*>(pg->property("ipinfo").value<QWidget*>());
        if (!info) return;
        if (!i.ok) { info->setText(tr("Lookup failed: %1").arg(i.error)); return; }
        info->setText(tr(
            "<b>%1</b><br>%2, %3, %4<br>ISP: %5<br>ASN: %6<br>TZ: %7")
            .arg(i.ip, i.city, i.region, i.country,
                 i.isp.isEmpty() ? i.org : i.isp,
                 i.asn, i.timezone));
    });

    connect(online_, &krad::OnlineServices::speedStage, this,
            [pg](const QString& s) {
        if (auto* l = qobject_cast<QLabel*>(pg->property("speed_stage").value<QWidget*>()))
            if (!s.isEmpty()) l->setText(s);
        if (auto* b = qobject_cast<QPushButton*>(pg->property("speed_btn").value<QWidget*>()))
            b->setEnabled(false);
    });
    connect(online_, &krad::OnlineServices::speedProgress, this,
            [pg](double mbps) {
        if (auto* p = qobject_cast<QProgressBar*>(pg->property("speed_prog").value<QWidget*>()))
            p->setValue(int(std::clamp(mbps, 0.0, 1000.0)));
    });
    connect(online_, &krad::OnlineServices::speedDone, this,
            [pg](const krad::SpeedResult& r) {
        if (auto* p = qobject_cast<QProgressBar*>(pg->property("speed_prog").value<QWidget*>()))
            p->setValue(0);
        if (auto* b = qobject_cast<QPushButton*>(pg->property("speed_btn").value<QWidget*>()))
            b->setEnabled(true);
        if (auto* l = qobject_cast<QLabel*>(pg->property("speed_res").value<QWidget*>())) {
            if (!r.ok) { l->setText(tr("Speed test failed")); return; }
            l->setText(tr("Ping %1 ms   •   Down %2 Mbps   •   Up %3 Mbps")
                .arg(r.ping_ms, 0, 'f', 0)
                .arg(r.down_mbps, 0, 'f', 1)
                .arg(r.up_mbps, 0, 'f', 1));
        }
    });

    connect(online_, &krad::OnlineServices::dnsProgress, this,
            [pg](int idx, int total) {
        if (auto* b = qobject_cast<QPushButton*>(pg->property("dns_btn").value<QWidget*>()))
            b->setEnabled(false);
        if (auto* l = qobject_cast<QLabel*>(pg->property("speed_stage").value<QWidget*>()))
            l->setText(tr("Benchmarking DNS server %1/%2…").arg(idx).arg(total));
    });
    connect(online_, &krad::OnlineServices::dnsDone, this,
            [pg](const std::vector<krad::DnsServerResult>& results) {
        if (auto* l = qobject_cast<QLabel*>(pg->property("speed_stage").value<QWidget*>()))
            l->setText(tr("DNS benchmark finished."));
        if (auto* tree = qobject_cast<QTreeWidget*>(pg->property("dns_tree").value<QWidget*>())) {
            tree->clear();
            double best = 1e9;
            for (auto& r : results)
                if (r.ok_count && r.avg_ms < best) best = r.avg_ms;
            for (auto& r : results) {
                auto* it = new QTreeWidgetItem({
                    r.server, r.name,
                    r.ok_count ? QString::number(r.avg_ms, 'f', 1) : "—",
                    r.ok_count ? QString::number(r.min_ms, 'f', 1) : "—",
                    QString("%1/%2").arg(r.ok_count).arg(r.total)});
                if (r.ok_count && r.avg_ms == best) {
                    for (int c = 0; c < 5; ++c) {
                        QFont f = it->font(c); f.setBold(true);
                        it->setFont(c, f);
                    }
                    it->setText(1, r.name + tr("  ★ fastest"));
                }
                tree->addTopLevelItem(it);
            }
        }
        if (auto* b = qobject_cast<QPushButton*>(pg->property("dns_btn").value<QWidget*>()))
            b->setEnabled(true);
    });

    connect(online_, &krad::OnlineServices::ntpDone, this,
            [pg](bool ok, double offMs, const QString& server) {
        if (auto* l = qobject_cast<QLabel*>(pg->property("ntp_res").value<QWidget*>())) {
            if (!ok) { l->setText(tr("NTP check failed: %1").arg(server)); return; }
            double a = qAbs(offMs);
            l->setText(tr("Offset: %1%2 ms via %3<br>%4")
                .arg(offMs >= 0 ? "+" : "−")
                .arg(a, 0, 'f', 1)
                .arg(server,
                     a < 100 ? tr("✔ Your clock is accurate.")
                             : tr("⚠ Consider enabling time sync.")));
        }
    });

    connect(online_, &krad::OnlineServices::updateCheckDone, this,
            [pg](bool newer, const QString& latest, const QString& notes,
                 const QString& error) {
        if (auto* l = qobject_cast<QLabel*>(pg->property("upd_res").value<QWidget*>())) {
            if (!error.isEmpty() && latest.isEmpty()) {
                l->setText(tr("Update check failed: %1").arg(error));
            } else if (newer) {
                l->setText(tr("<b>New version %1 available!</b> %2<br>"
                              "Download it from the downloads page.")
                    .arg(latest, notes));
            } else {
                l->setText(tr("✔ You are running the latest version (v%1).")
                    .arg(krad::APP_VERSION));
            }
        }
    });

    connect(online_, &krad::OnlineServices::reportShared, this,
            [pg](bool ok, const QString& urlOrError) {
        if (auto* l = qobject_cast<QLabel*>(pg->property("share_res").value<QWidget*>())) {
            if (ok) l->setText(tr("Shared: <a href=\"%1\">%1</a>").arg(urlOrError));
            else    l->setText(tr("Upload failed: %1").arg(urlOrError));
        }
    });

    connect(online_, &krad::OnlineServices::dashboardStateChanged, this,
            [pg](bool running, const QString& url) {
        auto* btn = qobject_cast<QPushButton*>(pg->property("dash_btn").value<QWidget*>());
        auto* res = qobject_cast<QLabel*>(pg->property("dash_res").value<QWidget*>());
        if (btn) btn->setText(running ? QObject::tr("Stop server")
                                      : QObject::tr("Start server"));
        if (res) {
            if (running)
                res->setText(QObject::tr("Dashboard live at:<br>"
                    "<a href=\"%1\"><b>%1</b></a><br>"
                    "Open it from any device on this network — "
                    "phone, tablet, laptop.").arg(url));
            else
                res->setText(QObject::tr("Server stopped."));
        }
    });
}

QByteArray MainWindow::dashboardStatsJson() {
    krad::PerfSample s = krad::collect::perf_sample();
    QJsonObject o;
    o["cpu"] = s.cpu_total;
    o["ram"] = s.ram_pct;
    o["gpu"] = s.gpu_pct;
    o["disk_active"] = s.disk_active;
    o["disk_read"] = s.disk_read_mbs;
    o["disk_write"] = s.disk_write_mbs;
    o["net_rx"] = s.net_rx_kbps;
    o["net_tx"] = s.net_tx_kbps;
    o["cores"] = int(report_.cpu.cores_logical);
    o["hostname"] = QString::fromStdString(report_.computer.hostname);
    o["os"] = QString::fromStdString(report_.os.product_name);
    // live uptime: report value + time since collection
    {
        using namespace std::chrono;
        auto since = duration_cast<seconds>(
            steady_clock::now().time_since_epoch()).count() -
            last_refresh_sec_;
        o["uptime"] = QString::fromStdString(
            krad::format_duration_sec(report_.os.uptime_sec +
                                      std::uint64_t(since > 0 ? since : 0)));
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace ui
} // namespace krad

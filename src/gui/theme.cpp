// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - theme implementation (QSS)
#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace krad {
namespace ui {

Theme& Theme::instance() {
    static Theme t;
    return t;
}

void Theme::apply(ThemeKind kind) {
    kind_ = kind;
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;

    app->setStyle(QStyleFactory::create("Fusion"));
    QPalette p;

    if (kind == ThemeKind::Dark) {
        accent_ = "#4fc3f7";
        p.setColor(QPalette::Window,          QColor("#141821"));
        p.setColor(QPalette::WindowText,      QColor("#e2e8f0"));
        p.setColor(QPalette::Base,            QColor("#1b2230"));
        p.setColor(QPalette::AlternateBase,   QColor("#1f2736"));
        p.setColor(QPalette::ToolTipBase,     QColor("#232b3d"));
        p.setColor(QPalette::ToolTipText,     QColor("#e2e8f0"));
        p.setColor(QPalette::Text,            QColor("#dbe4f0"));
        p.setColor(QPalette::Button,          QColor("#1c2434"));
        p.setColor(QPalette::ButtonText,      QColor("#dbe4f0"));
        p.setColor(QPalette::BrightText,      Qt::white);
        p.setColor(QPalette::Link,            QColor(accent_));
        p.setColor(QPalette::Highlight,       QColor("#2563eb"));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor("#5d6b82"));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor("#54607a"));
    } else {
        accent_ = "#0369a1";
        p.setColor(QPalette::Window,          QColor("#f1f4f9"));
        p.setColor(QPalette::WindowText,      QColor("#1a2333"));
        p.setColor(QPalette::Base,            QColor("#ffffff"));
        p.setColor(QPalette::AlternateBase,   QColor("#f3f6fb"));
        p.setColor(QPalette::ToolTipBase,     QColor("#ffffff"));
        p.setColor(QPalette::ToolTipText,     QColor("#1a2333"));
        p.setColor(QPalette::Text,            QColor("#1f2937"));
        p.setColor(QPalette::Button,          QColor("#ffffff"));
        p.setColor(QPalette::ButtonText,      QColor("#1f2937"));
        p.setColor(QPalette::Link,            QColor(accent_));
        p.setColor(QPalette::Highlight,       QColor("#2563eb"));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor("#9ca3af"));
    }
    app->setPalette(p);

    bool dark = kind == ThemeKind::Dark;
    const char* panel  = dark ? "#1b2230" : "#ffffff";
    const char* border = dark ? "#28324a" : "#dde4ee";
    const char* hover  = dark ? "#232d42" : "#eef2fa";
    const char* dim    = dark ? "#8ea0bd" : "#64748b";
    const char* sidebg = dark ? "#10151f" : "#e7ecf5";
    const char* textc  = dark ? "#dbe4f0" : "#1f2937";

    QString qss = QStringLiteral(
        "* { outline: none; }"
        "QMainWindow { background: %1; }"
        "QWidget { color: %7; font-size: 13px; }"
        "QListWidget#sidebar {"
        "  background: %6; border: none; padding: 10px 6px; font-size: 13px;"
        "}"
        "QListWidget#sidebar::item {"
        "  padding: 9px 14px; margin: 2px 6px; border-radius: 8px;"
        "}"
        "QListWidget#sidebar::item:selected {"
        "  background: %5; color: white;"
        "}"
        "QListWidget#sidebar::item:hover:!selected { background: %4; }"
        "QScrollArea { border: none; background: transparent; }"
        "QTreeWidget, QTreeView {"
        "  background: %2; alternate-background-color: rgba(127,140,170,12);"
        "  border: 1px solid %3; border-radius: 8px; padding: 4px;"
        "}"
        "QTreeWidget::item { height: 26px; }"
        "QHeaderView::section {"
        "  background: %2; border: none; border-bottom: 1px solid %3;"
        "  padding: 6px 8px; font-weight: 600;"
        "}"
        "QPushButton {"
        "  background: %2; border: 1px solid %3; border-radius: 8px;"
        "  padding: 7px 16px; font-weight: 500;"
        "}"
        "QPushButton:hover { background: %4; }"
        "QPushButton:pressed { background: %5; color: white; }"
        "QPushButton:disabled { color: %8; }"
        "QLineEdit, QComboBox, QSpinBox {"
        "  background: %2; border: 1px solid %3; border-radius: 8px;"
        "  padding: 6px 10px; selection-background-color: %5;"
        "}"
        "QProgressBar {"
        "  background: %2; border: 1px solid %3; border-radius: 7px;"
        "  text-align: center; height: 15px; font-size: 11px;"
        "}"
        "QProgressBar::chunk { border-radius: 6px; background: %5; }"
        "QStatusBar { background: %6; border-top: 1px solid %3; }"
        "QMenuBar { background: %6; }"
        "QMenu { background: %2; border: 1px solid %3; border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 7px 26px; border-radius: 6px; }"
        "QMenu::item:selected { background: %5; color: white; }"
        "QTabWidget::pane { border: 1px solid %3; border-radius: 8px; }"
        "QTabBar::tab { padding: 8px 18px; }"
        "QLabel#welcomeTitle { font-size: 26px; font-weight: 700; }"
        "QGroupBox {"
        "  background: %2; border: 1px solid %3; border-radius: 10px;"
        "  margin-top: 12px; padding: 14px 10px 10px 10px; font-weight: 600;"
        "}"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px;"
        "  padding: 0 6px; color: %5; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %3; border-radius: 5px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: %8; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
    ).arg(sidebg).arg(panel).arg(border).arg(hover)
     .arg("#2563eb").arg(dark ? "#10151f" : "#e7ecf5")
     .arg(textc).arg(dim);

    app->setStyleSheet(qss);
}

} // namespace ui
} // namespace krad

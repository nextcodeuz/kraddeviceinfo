// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - theme implementation (QSS)
#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStringList>

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
        p.setColor(QPalette::Base,            QColor("#11161f"));
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
        p.setColor(QPalette::Window,          QColor("#eef2f8"));
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
        p.setColor(QPalette::Disabled, QPalette::Text, QColor("#9ca3af"));
    }
    app->setPalette(p);

    // QSS palettes. Tokens ([BG], [TXT], ...) are substituted below because
    // QString::arg only understands a single %1..%9 placeholder per call.
    const bool dark = kind == ThemeKind::Dark;
    QString bg     = dark ? "#10151f" : "#e7ecf5";   // window / sidebar
    QString panel  = dark ? "#1b2230" : "#ffffff";   // cards, trees
    QString edge   = dark ? "#28324a" : "#d4dde8";   // borders
    QString hover  = dark ? "#232d42" : "#eef2fa";   // hover fill
    QString blue   = dark ? "#1f6feb" : "#2563eb";   // selection / focus
    QString in     = dark ? "#11161f" : "#ffffff";   // input background
    QString dim    = dark ? "#8ea0bd" : "#64748b";   // secondary text
    QString txt    = dark ? "#dbe4f0" : "#1f2937";   // primary text
    QString acc    = dark ? "#4fc3f7" : "#0369a1";   // accent / links

    QString qss = QString::fromLatin1(
        "* { outline: none; "
              "font-family: 'Segoe UI', 'Segoe UI Variable Text', system-ui, "
                        "-apple-system, 'Noto Sans', sans-serif; }"
        "QMainWindow { background: [BG]; }"
        "QWidget { color: [TXT]; font-size: 13px; }"
        "QToolTip { background: [PANEL]; color: [TXT]; border: 1px solid [EDGE];"
                   "border-radius: 6px; padding: 6px 9px; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { border: none;"
                                                       "background: transparent; }"
        "QListWidget#sidebar {"
        "  background: [BG]; border: none; border-right: 1px solid [EDGE];"
        "  padding: 12px 6px; font-size: 13px;"
        "}"
        "QListWidget#sidebar::item {"
        "  padding: 10px 14px; margin: 3px 6px; border-radius: 9px;"
        "}"
        "QListWidget#sidebar::item:selected {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "    stop:0 [BLUE], stop:1 #1d4ed8);"
        "  color: #ffffff; font-weight: 600;"
        "}"
        "QListWidget#sidebar::item:hover:!selected {"
        "  background: rgba(127,149,190,0.16);"
        "}"
        "QTreeWidget, QTreeView {"
        "  background: [PANEL]; alternate-background-color: rgba(127,140,170,14);"
        "  border: 1px solid [EDGE]; border-radius: 10px; padding: 4px;"
        "  selection-background-color: rgba(37,99,235,0.28); selection-color: [TXT];"
        "}"
        "QTreeWidget::item { height: 28px; border-radius: 6px; padding: 0 4px; }"
        "QTreeWidget::item:hover { background: rgba(127,149,190,0.12); }"
        "QTreeWidget::item:selected { background: rgba(37,99,235,0.32); color: [TXT]; }"
        "QTreeWidget::branch { background: transparent; }"
        "QHeaderView::section {"
        "  background: transparent; border: none; border-bottom: 1px solid [EDGE];"
        "  padding: 7px 10px; font-weight: 600; color: [DIM];"
        "}"
        "QHeaderView::section:hover { color: [TXT]; }"
        "QPushButton {"
        "  background: [PANEL]; border: 1px solid [EDGE]; border-radius: 9px;"
        "  padding: 8px 18px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: [HOV]; border-color: [BLUE]; }"
        "QPushButton:pressed { background: #1d4ed8; color: #ffffff;"
        "  border-color: #1d4ed8; }"
        "QPushButton:disabled { color: [DIM]; border-color: [EDGE]; background: [PANEL]; }"
        "QPushButton:focus { border: 1px solid [BLUE]; }"
        "QLineEdit, QComboBox, QSpinBox {"
        "  background: [IN]; border: 1px solid [EDGE]; border-radius: 9px;"
        "  padding: 7px 12px; selection-background-color: [BLUE];"
        "  selection-color: #ffffff;"
        "}"
        "QLineEdit:hover, QComboBox:hover, QSpinBox:hover,"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: [BLUE]; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QProgressBar {"
        "  background: [PANEL]; border: 1px solid [EDGE]; border-radius: 8px;"
        "  text-align: center; font-size: 11px; height: 18px;"
        "}"
        "QProgressBar::chunk {"
        "  border-radius: 7px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #2563eb, stop:1 [ACC]);"
        "}"
        "QStatusBar { background: [BG]; border-top: 1px solid [EDGE]; color: [DIM]; }"
        "QMenuBar { background: [BG]; border-bottom: 1px solid [EDGE]; }"
        "QMenuBar::item { padding: 7px 12px; border-radius: 7px;"
        "  background: transparent; }"
        "QMenuBar::item:selected { background: [HOV]; }"
        "QMenu { background: [PANEL]; border: 1px solid [EDGE]; border-radius: 10px;"
        "  padding: 6px; }"
        "QMenu::item { padding: 7px 26px; border-radius: 7px; }"
        "QMenu::item:selected { background: #2563eb; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: [EDGE]; margin: 5px 8px; }"
        "QTabWidget::pane { border: 1px solid [EDGE]; border-radius: 10px; }"
        "QTabBar::tab { padding: 8px 18px; }"
        "QGroupBox {"
        "  background: [PANEL]; border: 1px solid [EDGE]; border-radius: 12px;"
        "  margin-top: 14px; padding: 16px 12px 12px 12px; font-weight: 600;"
        "}"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; top: 0px;"
        "  padding: 0 8px; color: [ACC]; font-weight: 700; }"
        "QToolBar { background: transparent; border: none; spacing: 4px;"
        "  padding: 4px 6px; }"
        "QToolBar::separator { background: [EDGE]; width: 1px; margin: 6px 8px; }"
        "QToolButton { padding: 7px 12px; border-radius: 9px;"
        "  font-weight: 600; border: none; }"
        "QToolButton:hover { background: [HOV]; }"
        "QToolButton:pressed, QToolButton:checked { background: #2563eb;"
        "  color: #ffffff; }"
        "QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: [EDGE]; border-radius: 4px;"
        "  min-height: 28px; }"
        "QScrollBar::handle:vertical:hover { background: [DIM]; }"
        "QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: [EDGE]; border-radius: 4px;"
        "  min-width: 28px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
        "QLabel#page-title { font-size: 24px; font-weight: 700; }"
        "QLabel a { color: [ACC]; }"
        "QSplitter::handle { background: transparent; width: 8px; height: 8px; }"
        "QMessageBox { background: [BG]; }"
        "QStatusBar::item { border: none; }");

    const QStringList tokens = {
        "[BG]",    bg,
        "[PANEL]", panel,
        "[EDGE]",  edge,
        "[HOV]",   hover,
        "[BLUE]",  blue,
        "[IN]",    in,
        "[DIM]",   dim,
        "[TXT]",   txt,
        "[ACC]",   acc,
    };
    for (int i = 0; i < tokens.size(); i += 2)
        qss.replace(tokens[i], tokens[i + 1]);

    app->setStyleSheet(qss);
}

} // namespace ui
} // namespace krad
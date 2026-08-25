// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - shared page building blocks
#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QLineEdit>

#include <krad/model.h>

namespace krad {
namespace ui {

// renders a list of ReportSections as group boxes of key/value rows
class SectionView : public QWidget {
    Q_OBJECT
public:
    explicit SectionView(QWidget* parent = nullptr);
    void setSections(const std::vector<ReportSection>& sections);
};

// searchable tree of sections (used on CPU/Storage/Devices pages)
class KVTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit KVTree(QWidget* parent = nullptr);
    void populate(const std::vector<ReportSection>& sections);
    void setFilter(const QString& text);

private:
    void applyFilter();
    std::vector<QTreeWidgetItem*> items_;
    QString filter_;
};

class PageTitle : public QLabel {
    Q_OBJECT
public:
    explicit PageTitle(const QString& text, QWidget* parent = nullptr);
};

} // namespace ui
} // namespace krad

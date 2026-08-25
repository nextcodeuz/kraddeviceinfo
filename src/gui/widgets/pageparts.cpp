// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - shared page parts
#include "pageparts.h"
#include "../theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHeaderView>

namespace krad {
namespace ui {

SectionView::SectionView(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(12);
}

void SectionView::setSections(const std::vector<ReportSection>& sections) {
    // clear old layout children
    if (layout()) {
        QLayoutItem* it;
        while ((it = layout()->takeAt(0)) != nullptr) {
            if (auto* w = it->widget()) w->deleteLater();
            delete it;
        }
    }
    auto* outer = qobject_cast<QVBoxLayout*>(layout());
    if (!outer) { outer = new QVBoxLayout(this); }

    for (const auto& s : sections) {
        auto* box = new QGroupBox(QString::fromStdString(s.title));
        auto* grid = new QVBoxLayout(box);
        grid->setSpacing(3);
        for (const auto& row : s.rows) {
            auto* h = new QHBoxLayout();
            h->setSpacing(10);
            auto* k = new QLabel(QString::fromStdString(row.key));
            k->setStyleSheet("color: palette(placeholder-text); min-width: 150px;");
            k->setAlignment(Qt::AlignTop);
            auto* v = new QLabel(QString::fromStdString(row.value));
            v->setTextInteractionFlags(Qt::TextSelectableByMouse);
            v->setWordWrap(true);
            v->setAlignment(Qt::AlignTop);
            h->addWidget(k);
            h->addWidget(v, 1);
            grid->addLayout(h);
        }
        outer->addWidget(box);
    }
    outer->addStretch(1);
}

KVTree::KVTree(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(2);
    setHeaderLabels({tr("Property"), tr("Value")});
    setRootIsDecorated(false);
    setAlternatingRowColors(true);
    setUniformRowHeights(true);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setStretchLastSection(true);
}

void KVTree::populate(const std::vector<ReportSection>& sections) {
    setUpdatesEnabled(false);
    clear();
    items_.clear();
    items_.reserve(sections.size() * 8);
    QList<QTreeWidgetItem*> top;
    for (const auto& s : sections) {
        auto* title = new QTreeWidgetItem(
            {QString::fromStdString(s.title), ""});
        QFont f = title->font(0);
        f.setBold(true);
        title->setFont(0, f);
        title->setFirstColumnSpanned(true);
        items_.push_back(title);
        QList<QTreeWidgetItem*> children;
        children.reserve(int(s.rows.size()));
        for (const auto& row : s.rows) {
            auto* it = new QTreeWidgetItem(
                {QString::fromStdString(row.key),
                 QString::fromStdString(row.value)});
            children.push_back(it);
            items_.push_back(it);
        }
        title->addChildren(children);
        top.push_back(title);
    }
    addTopLevelItems(top);
    applyFilter();
    setUpdatesEnabled(true);
}

void KVTree::setFilter(const QString& text) {
    filter_ = text.trimmed();
    applyFilter();
}

void KVTree::applyFilter() {
    const QString f = filter_;
    bool any_hidden_group = false;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        auto* grp = topLevelItem(i);
        bool group_visible = false;
        for (int j = 0; j < grp->childCount(); ++j) {
            auto* c = grp->child(j);
            bool vis = f.isEmpty() ||
                       c->text(0).contains(f, Qt::CaseInsensitive) ||
                       c->text(1).contains(f, Qt::CaseInsensitive);
            c->setHidden(!vis);
            group_visible |= vis;
        }
        // when no children match hide the whole group
        bool empty_group = grp->childCount() == 0;
        grp->setHidden(!f.isEmpty() && !group_visible && !empty_group &&
                       !grp->text(0).contains(f, Qt::CaseInsensitive));
        any_hidden_group |= grp->isHidden();
    }
    (void)any_hidden_group;
}

PageTitle::PageTitle(const QString& text, QWidget* parent) :
    QLabel(text, parent) {
    setObjectName("page-title");
    QFont f = font();
    f.setPixelSize(22);
    f.setBold(true);
    setFont(f);
}

} // namespace ui
} // namespace krad

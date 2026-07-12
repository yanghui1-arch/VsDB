#include "ui/InspectorPane.h"

#include "models/InspectorModel.h"
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>

namespace vsdb {

InspectorPane::InspectorPane(InspectorModel *model, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("inspectorPane"));
    setMinimumWidth(230);
    setMaximumWidth(360);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 12, 12);
    layout->setSpacing(6);
    auto *title = new QLabel(tr("INSPECTOR"), this);
    title->setObjectName(QStringLiteral("paneTitle"));
    nameLabel_ = new QLabel(this);
    nameLabel_->setObjectName(QStringLiteral("inspectorName"));
    subtitleLabel_ = new QLabel(this);
    subtitleLabel_->setObjectName(QStringLiteral("inspectorSubtitle"));
    emptyLabel_ = new QLabel(tr("Select a table or view to inspect its preview metadata."), this);
    emptyLabel_->setWordWrap(true);
    tableView_ = new QTableView(this);
    tableView_->setObjectName(QStringLiteral("inspectorTable"));
    tableView_->setModel(model);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setSelectionMode(QAbstractItemView::NoSelection);
    tableView_->verticalHeader()->hide();
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(nameLabel_);
    layout->addWidget(subtitleLabel_);
    layout->addSpacing(10);
    layout->addWidget(emptyLabel_);
    layout->addWidget(tableView_, 1);
}

void InspectorPane::setSelection(const QString &name, const QString &subtitle, bool inspectable)
{
    nameLabel_->setText(inspectable ? name : tr("No object selected"));
    subtitleLabel_->setText(subtitle);
    tableView_->setVisible(inspectable);
    emptyLabel_->setVisible(!inspectable);
}

} // namespace vsdb

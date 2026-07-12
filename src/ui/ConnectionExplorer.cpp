#include "ui/ConnectionExplorer.h"

#include "models/SchemaTreeModel.h"
#include <QItemSelectionModel>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace vsdb {

ConnectionExplorer::ConnectionExplorer(SchemaTreeModel *model, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("connectionExplorer"));
    setMinimumWidth(210);
    setMaximumWidth(340);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 10, 12);
    layout->setSpacing(10);
    auto *title = new QLabel(tr("CONNECTIONS"), this);
    title->setObjectName(QStringLiteral("paneTitle"));
    layout->addWidget(title);

    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model);
    proxyModel_->setRecursiveFilteringEnabled(true);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterRole(Qt::DisplayRole);

    treeView_ = new QTreeView(this);
    treeView_->setObjectName(QStringLiteral("schemaTree"));
    treeView_->setAccessibleName(tr("Database objects"));
    treeView_->setModel(proxyModel_);
    treeView_->setHeaderHidden(true);
    treeView_->setUniformRowHeights(true);
    treeView_->setAnimated(false);
    treeView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    treeView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(treeView_, 1);

    connect(treeView_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        const auto source = proxyModel_->mapToSource(current);
        emit objectSelected(source.data(SchemaTreeModel::ObjectIdRole).toString(), source.data().toString());
    });
    connect(treeView_, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const auto source = proxyModel_->mapToSource(index);
        const auto kind = static_cast<SchemaNodeKind>(source.data(SchemaTreeModel::KindRole).toInt());
        if (kind == SchemaNodeKind::Table || kind == SchemaNodeKind::View)
            emit tableActivated(source.data().toString());
    });
    expandDefaults();
}

void ConnectionExplorer::setFilterText(const QString &text)
{
    proxyModel_->setFilterFixedString(text);
    if (!text.isEmpty())
        treeView_->expandAll();
}

QTreeView *ConnectionExplorer::treeView() const { return treeView_; }

void ConnectionExplorer::expandDefaults()
{
    treeView_->expandToDepth(5);
    const auto matches = proxyModel_->match(proxyModel_->index(0, 0), Qt::DisplayRole, QStringLiteral("users"), 1, Qt::MatchRecursive | Qt::MatchExactly);
    if (!matches.isEmpty())
        treeView_->setCurrentIndex(matches.first());
}

} // namespace vsdb

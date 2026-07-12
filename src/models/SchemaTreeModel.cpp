#include "models/SchemaTreeModel.h"

#include "core/MockWorkspaceData.h"
#include <QApplication>
#include <QStyle>

namespace vsdb {

SchemaTreeModel::SchemaTreeModel(QObject *parent) : QAbstractItemModel(parent), root_(mock::schemaTree()) {}

QModelIndex SchemaTreeModel::index(int row, int column, const QModelIndex &parentIndex) const
{
    if (column != 0 || row < 0)
        return {};
    auto *parentNode = nodeForIndex(parentIndex);
    if (!parentNode || static_cast<std::size_t>(row) >= parentNode->children.size())
        return {};
    return createIndex(row, column, parentNode->children.at(static_cast<std::size_t>(row)).get());
}

QModelIndex SchemaTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    auto *node = nodeForIndex(child);
    auto *parentNode = node ? node->parent : nullptr;
    if (!parentNode || parentNode == root_.get())
        return {};
    auto *grandparent = parentNode->parent;
    if (!grandparent)
        return {};
    for (std::size_t row = 0; row < grandparent->children.size(); ++row) {
        if (grandparent->children.at(row).get() == parentNode)
            return createIndex(static_cast<int>(row), 0, parentNode);
    }
    return {};
}

int SchemaTreeModel::rowCount(const QModelIndex &parentIndex) const
{
    if (parentIndex.column() > 0)
        return 0;
    auto *parentNode = nodeForIndex(parentIndex);
    return parentNode ? static_cast<int>(parentNode->children.size()) : 0;
}

int SchemaTreeModel::columnCount(const QModelIndex &) const { return 1; }

QVariant SchemaTreeModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid())
        return {};
    const auto *node = nodeForIndex(modelIndex);
    if (!node)
        return {};
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
        return node->name;
    if (role == ObjectIdRole)
        return node->id;
    if (role == KindRole)
        return static_cast<int>(node->kind);
    if (role == Qt::DecorationRole) {
        auto *style = QApplication::style();
        switch (node->kind) {
        case SchemaNodeKind::Connection: return style->standardIcon(QStyle::SP_DriveNetIcon);
        case SchemaNodeKind::Database: return style->standardIcon(QStyle::SP_DriveHDIcon);
        case SchemaNodeKind::Table: return style->standardIcon(QStyle::SP_FileDialogDetailedView);
        case SchemaNodeKind::View: return style->standardIcon(QStyle::SP_FileDialogContentsView);
        default: return style->standardIcon(QStyle::SP_DirIcon);
        }
    }
    return {};
}

Qt::ItemFlags SchemaTreeModel::flags(const QModelIndex &modelIndex) const
{
    return modelIndex.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

void SchemaTreeModel::reload()
{
    beginResetModel();
    root_ = mock::schemaTree();
    endResetModel();
}

SchemaNode *SchemaTreeModel::nodeForIndex(const QModelIndex &modelIndex) const
{
    return modelIndex.isValid() ? static_cast<SchemaNode *>(modelIndex.internalPointer()) : root_.get();
}

} // namespace vsdb

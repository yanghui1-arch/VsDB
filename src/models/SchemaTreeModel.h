#pragma once

#include "core/WorkspaceData.h"
#include <QAbstractItemModel>

namespace vsdb {

class SchemaTreeModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Role { ObjectIdRole = Qt::UserRole + 1, KindRole };
    explicit SchemaTreeModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void reload();

private:
    SchemaNode *nodeForIndex(const QModelIndex &index) const;
    std::unique_ptr<SchemaNode> root_;
};

} // namespace vsdb

#pragma once

#include "core/WorkspaceData.h"
#include <QAbstractTableModel>

namespace vsdb {

class ResultTableModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ResultTableModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void setResult(QueryResult result);
    void clear();
    int elapsedMs() const;

private:
    QueryResult result_;
};

} // namespace vsdb

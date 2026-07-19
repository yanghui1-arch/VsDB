#pragma once

#include "database/DatabaseTypes.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QHeaderView>

namespace vsdb {

class ResultTableModel final : public QAbstractTableModel
{
public:
    explicit ResultTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void beginResult(QVector<QueryColumn> columns, bool select,
                     bool editable = false);
    void appendRows(QVector<QVariantList> rows);
    void setResult(QueryResult result, bool editable = false);
    const QVector<QueryColumn> &columns() const;
    const QVector<QVariantList> &originalRows() const;
    QVector<CellChange> pendingChanges() const;
    int pendingChangeCount() const;
    bool isEditable() const;
    void commitPendingChanges();
    void rollbackPendingChanges();

private:
    static quint64 cellKey(int row, int column);
    static int keyRow(quint64 key);
    static int keyColumn(quint64 key);
    static QVariant displayValue(const QVariant &value);
    QVariant normalizedValue(const QModelIndex &index, const QVariant &value) const;
    void notifyChangedCells(const QList<quint64> &keys);

    QueryResult result_;
    QHash<quint64, QVariant> pendingValues_;
    bool editable_ = false;
};

class TwoLineHeaderView final : public QHeaderView
{
public:
    explicit TwoLineHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    void paintSection(QPainter *painter, const QRect &rect,
                      int logicalIndex) const override;
};

} // namespace vsdb

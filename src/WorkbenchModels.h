#pragma once

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

    void setVisibleRows(int rows);
    int pendingChangeCount() const;
    void commitPendingChanges();
    void rollbackPendingChanges();

private:
    static quint64 cellKey(int row, int column);
    QVariant generatedValue(const QModelIndex &index) const;
    QVariant normalizedValue(const QModelIndex &index, const QVariant &value) const;
    void notifyChangedCells(const QList<quint64> &keys);

    int visibleRows_ = 100;
    QHash<quint64, QVariant> committedValues_;
    QHash<quint64, QVariant> pendingValues_;
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

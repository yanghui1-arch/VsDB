#include "models/ResultTableModel.h"

#include <QBrush>
#include <QColor>
#include <utility>

namespace vsdb {

ResultTableModel::ResultTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int ResultTableModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : result_.rows.size(); }
int ResultTableModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : result_.columns.size(); }

QVariant ResultTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= result_.rows.size() || index.column() >= result_.columns.size())
        return {};
    const QVariant value = result_.rows.at(index.row()).value(index.column());
    if (role == Qt::DisplayRole)
        return value.isNull() ? QVariant(QStringLiteral("NULL")) : value;
    if (role == Qt::ToolTipRole)
        return value.isNull() ? QStringLiteral("SQL NULL") : value.toString();
    if (role == Qt::TextAlignmentRole && (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::LongLong || value.metaType().id() == QMetaType::Double))
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ForegroundRole && value.isNull())
        return QBrush(QColor(QStringLiteral("#8a94a6")));
    return {};
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal && section >= 0 && section < result_.columns.size())
        return result_.columns.at(section).name;
    return section + 1;
}

Qt::ItemFlags ResultTableModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

void ResultTableModel::setResult(QueryResult result)
{
    beginResetModel();
    result_ = std::move(result);
    endResetModel();
}

void ResultTableModel::clear()
{
    setResult({});
}

int ResultTableModel::elapsedMs() const { return result_.elapsedMs; }

} // namespace vsdb

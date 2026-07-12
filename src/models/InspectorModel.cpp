#include "models/InspectorModel.h"
#include <utility>

namespace vsdb {

InspectorModel::InspectorModel(QObject *parent) : QAbstractTableModel(parent) {}
int InspectorModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : properties_.size(); }
int InspectorModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 2; }

QVariant InspectorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= properties_.size())
        return {};
    const auto &property = properties_.at(index.row());
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
        return index.column() == 0 ? property.name : property.value;
    return {};
}

QVariant InspectorModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return section == 0 ? tr("Property") : tr("Value");
}

Qt::ItemFlags InspectorModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

void InspectorModel::setProperties(QList<InspectorProperty> properties)
{
    beginResetModel();
    properties_ = std::move(properties);
    endResetModel();
}

} // namespace vsdb

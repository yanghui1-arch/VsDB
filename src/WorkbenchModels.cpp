#include "WorkbenchModels.h"

#include <QColor>
#include <QPainter>
#include <QStringList>

namespace vsdb {

namespace {
const QStringList kNames{
    QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol"),
    QStringLiteral("dave"), QStringLiteral("eve"), QStringLiteral("frank"),
    QStringLiteral("grace"), QStringLiteral("heidi"), QStringLiteral("ivan"),
    QStringLiteral("judy"), QStringLiteral("mallory"), QStringLiteral("oscar")};
const QStringList kStatuses{
    QStringLiteral("completed"), QStringLiteral("shipped"), QStringLiteral("completed"),
    QStringLiteral("pending"), QStringLiteral("completed"), QStringLiteral("shipped"),
    QStringLiteral("cancelled"), QStringLiteral("completed")};
const QStringList kHeaders{
    QStringLiteral("id\nint8"), QStringLiteral("email\ntext"),
    QStringLiteral("created_at\ntimestamptz"), QStringLiteral("order_id\nint8"),
    QStringLiteral("total\nnumeric"), QStringLiteral("status\ntext")};
}

ResultTableModel::ResultTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int ResultTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : visibleRows_;
}

int ResultTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : kHeaders.size();
}

QVariant ResultTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= visibleRows_)
        return {};

    if (role == Qt::TextAlignmentRole && (index.column() == 0 || index.column() == 3 || index.column() == 4))
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    const quint64 key = cellKey(index.row(), index.column());
    if (role == Qt::BackgroundRole && pendingValues_.contains(key))
        return QColor(QStringLiteral("#3A3020"));
    if (role == Qt::ToolTipRole && pendingValues_.contains(key))
        return QStringLiteral("Pending change — apply or revert the transaction");
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    if (const auto pending = pendingValues_.constFind(key); pending != pendingValues_.constEnd())
        return pending.value();
    if (const auto committed = committedValues_.constFind(key); committed != committedValues_.constEnd())
        return committed.value();
    return generatedValue(index);
}

bool ResultTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.row() >= visibleRows_)
        return false;

    const QVariant normalized = normalizedValue(index, value);
    if (!normalized.isValid())
        return false;

    const quint64 key = cellKey(index.row(), index.column());
    const QVariant committed = committedValues_.value(key, generatedValue(index));
    if (normalized == committed)
        pendingValues_.remove(key);
    else
        pendingValues_.insert(key, normalized);

    emit dataChanged(index, index,
                     {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole, Qt::ToolTipRole});
    return true;
}

Qt::ItemFlags ResultTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return QAbstractTableModel::flags(index);
    return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
}

quint64 ResultTableModel::cellKey(int row, int column)
{
    return (static_cast<quint64>(static_cast<quint32>(row)) << 32)
        | static_cast<quint32>(column);
}

QVariant ResultTableModel::generatedValue(const QModelIndex &index) const
{
    const int row = index.row();
    const QString &name = kNames.at(row % kNames.size());

    switch (index.column()) {
    case 0: return static_cast<qlonglong>(1001 + row);
    case 1: return name + QStringLiteral("@example.com");
    case 2:
        return QStringLiteral("2026-07-%1 %2:%3:%4+08")
            .arg(12 - (row % 8), 2, 10, QLatin1Char('0'))
            .arg(9 + (row % 10), 2, 10, QLatin1Char('0'))
            .arg((row * 7) % 60, 2, 10, QLatin1Char('0'))
            .arg((row * 13) % 60, 2, 10, QLatin1Char('0'));
    case 3: return static_cast<qlonglong>(5001 + row);
    case 4: return QString::number(15.0 + ((row * 1749) % 18500) / 100.0, 'f', 2);
    case 5: return kStatuses.at(row % kStatuses.size());
    default: return {};
    }
}

QVariant ResultTableModel::normalizedValue(const QModelIndex &index, const QVariant &value) const
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return {};

    if (index.column() == 0 || index.column() == 3) {
        bool ok = false;
        const qlonglong number = text.toLongLong(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    if (index.column() == 4) {
        bool ok = false;
        const double number = text.toDouble(&ok);
        return ok ? QVariant(QString::number(number, 'f', 2)) : QVariant{};
    }
    return text;
}

int ResultTableModel::pendingChangeCount() const
{
    return pendingValues_.size();
}

void ResultTableModel::commitPendingChanges()
{
    const QList<quint64> keys = pendingValues_.keys();
    for (auto it = pendingValues_.constBegin(); it != pendingValues_.constEnd(); ++it)
        committedValues_.insert(it.key(), it.value());
    pendingValues_.clear();
    notifyChangedCells(keys);
}

void ResultTableModel::rollbackPendingChanges()
{
    const QList<quint64> keys = pendingValues_.keys();
    pendingValues_.clear();
    notifyChangedCells(keys);
}

void ResultTableModel::notifyChangedCells(const QList<quint64> &keys)
{
    for (const quint64 key : keys) {
        const int row = static_cast<int>(key >> 32);
        const int column = static_cast<int>(key & 0xffffffffu);
        if (row < visibleRows_)
            emit dataChanged(index(row, column), index(row, column),
                             {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole, Qt::ToolTipRole});
    }
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal && section >= 0 && section < kHeaders.size())
            return kHeaders.at(section);
        if (orientation == Qt::Vertical)
            return section + 1;
    }
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    return {};
}

void ResultTableModel::setVisibleRows(int rows)
{
    rows = qBound(1, rows, 1000);
    if (rows == visibleRows_)
        return;
    beginResetModel();
    visibleRows_ = rows;
    endResetModel();
}

TwoLineHeaderView::TwoLineHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

void TwoLineHeaderView::paintSection(QPainter *painter, const QRect &rect,
                                     int logicalIndex) const
{
    if (!rect.isValid())
        return;

    painter->save();
    painter->fillRect(rect, QColor(QStringLiteral("#24252E")));
    painter->setPen(QColor(QStringLiteral("#30323C")));
    painter->drawLine(rect.topRight(), rect.bottomRight());
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    const QString text = model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString();
    const qsizetype separator = text.indexOf(QLatin1Char('\n'));
    const QString name = separator >= 0 ? text.left(separator) : text;
    const QString type = separator >= 0 ? text.mid(separator + 1) : QString{};
    const QRect content = rect.adjusted(10, 5, -7, -4);
    const int lineHeight = content.height() / 2;

    QFont nameFont = font();
    nameFont.setWeight(QFont::DemiBold);
    painter->setFont(nameFont);
    painter->setPen(QColor(QStringLiteral("#B7BAC5")));
    painter->drawText(QRect(content.left(), content.top(), content.width(), lineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter, name);

    QFont typeFont = font();
    typeFont.setWeight(QFont::Normal);
    painter->setFont(typeFont);
    painter->setPen(QColor(QStringLiteral("#747885")));
    painter->drawText(QRect(content.left(), content.top() + lineHeight,
                            content.width(), content.height() - lineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter, type);
    painter->restore();
}

} // namespace vsdb

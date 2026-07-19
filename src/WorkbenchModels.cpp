#include "WorkbenchModels.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QPainter>
#include <QTime>
#include <QUuid>

#include <utility>

namespace vsdb {

ResultTableModel::ResultTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int ResultTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : result_.rows.size();
}

int ResultTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : result_.columns.size();
}

QVariant ResultTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= result_.rows.size()
        || index.column() >= result_.columns.size())
        return {};

    const quint64 key = cellKey(index.row(), index.column());
    const QVariant value = pendingValues_.contains(key)
        ? pendingValues_.value(key) : result_.rows.at(index.row()).at(index.column());

    if (role == Qt::BackgroundRole && pendingValues_.contains(key))
        return QColor(QStringLiteral("#3A3020"));
    if (role == Qt::ToolTipRole) {
        if (pendingValues_.contains(key))
            return QStringLiteral("待提交修改");
        if (value.metaType().id() == QMetaType::QString && value.toString().size() > 512)
            return QStringLiteral("完整内容包含 %1 个字符；复制、导出或编辑时仍使用完整值。")
                .arg(value.toString().size());
        if (value.metaType().id() == QMetaType::QByteArray)
            return QStringLiteral("二进制内容共 %1 字节；导出时仍使用完整值。")
                .arg(value.toByteArray().size());
    }
    if (role == Qt::ForegroundRole && value.isNull())
        return QColor(QStringLiteral("#747885"));
    if (role == Qt::TextAlignmentRole) {
        switch (value.metaType().id()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Double:
        case QMetaType::Float:
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (role == Qt::DisplayRole && value.isNull())
        return QStringLiteral("NULL");
    return role == Qt::DisplayRole ? displayValue(value) : value;
}

bool ResultTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !editable_ || !index.isValid()
        || index.row() >= result_.rows.size() || index.column() >= result_.columns.size())
        return false;

    const quint64 key = cellKey(index.row(), index.column());
    const QVariant original = result_.rows.at(index.row()).at(index.column());
    const QVariant normalized = normalizedValue(index, value);
    if (!normalized.isValid())
        return false;

    const bool hadPendingValue = pendingValues_.contains(key);
    const bool unchanged = (normalized.isNull() && original.isNull())
        || normalized == original;
    if (unchanged) {
        if (!hadPendingValue)
            return true;
        pendingValues_.remove(key);
    } else {
        if (hadPendingValue && pendingValues_.value(key) == normalized)
            return true;
        pendingValues_.insert(key, normalized);
    }

    emit dataChanged(index, index,
                     {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole,
                      Qt::ForegroundRole, Qt::ToolTipRole});
    return true;
}

Qt::ItemFlags ResultTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags result = QAbstractTableModel::flags(index);
    if (index.isValid() && editable_)
        result |= Qt::ItemIsEditable;
    return result;
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal && section >= 0 && section < result_.columns.size()) {
            const QueryColumn &column = result_.columns.at(section);
            return column.name + QLatin1Char('\n') + column.type;
        }
        if (orientation == Qt::Vertical)
            return section + 1;
    }
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    return {};
}

void ResultTableModel::setResult(QueryResult result, bool editable)
{
    beginResetModel();
    result_ = std::move(result);
    pendingValues_.clear();
    editable_ = editable;
    endResetModel();
}

void ResultTableModel::beginResult(QVector<QueryColumn> columns, bool select,
                                   bool editable)
{
    beginResetModel();
    result_ = {};
    result_.columns = std::move(columns);
    result_.select = select;
    pendingValues_.clear();
    editable_ = editable;
    endResetModel();
}

void ResultTableModel::appendRows(QVector<QVariantList> rows)
{
    if (rows.isEmpty())
        return;

    const int first = result_.rows.size();
    const int last = first + rows.size() - 1;
    beginInsertRows({}, first, last);
    result_.rows.reserve(last + 1);
    for (QVariantList &row : rows)
        result_.rows.append(std::move(row));
    endInsertRows();
}

const QVector<QueryColumn> &ResultTableModel::columns() const
{
    return result_.columns;
}

const QVector<QVariantList> &ResultTableModel::originalRows() const
{
    return result_.rows;
}

QVector<CellChange> ResultTableModel::pendingChanges() const
{
    QVector<CellChange> changes;
    changes.reserve(pendingValues_.size());
    for (auto pending = pendingValues_.constBegin(); pending != pendingValues_.constEnd(); ++pending) {
        const int row = keyRow(pending.key());
        const int column = keyColumn(pending.key());
        changes.append({row, column, result_.rows.at(row).at(column), pending.value()});
    }
    return changes;
}

int ResultTableModel::pendingChangeCount() const
{
    return pendingValues_.size();
}

bool ResultTableModel::isEditable() const
{
    return editable_;
}

void ResultTableModel::commitPendingChanges()
{
    const QList<quint64> keys = pendingValues_.keys();
    for (auto pending = pendingValues_.constBegin(); pending != pendingValues_.constEnd(); ++pending)
        result_.rows[keyRow(pending.key())][keyColumn(pending.key())] = pending.value();
    pendingValues_.clear();
    notifyChangedCells(keys);
}

void ResultTableModel::rollbackPendingChanges()
{
    const QList<quint64> keys = pendingValues_.keys();
    pendingValues_.clear();
    notifyChangedCells(keys);
}

quint64 ResultTableModel::cellKey(int row, int column)
{
    return (static_cast<quint64>(static_cast<quint32>(row)) << 32)
        | static_cast<quint32>(column);
}

int ResultTableModel::keyRow(quint64 key)
{
    return static_cast<int>(key >> 32);
}

int ResultTableModel::keyColumn(quint64 key)
{
    return static_cast<int>(key & 0xffffffffu);
}

QVariant ResultTableModel::displayValue(const QVariant &value)
{
    constexpr qsizetype previewLength = 512;
    if (value.metaType().id() == QMetaType::QString) {
        const QString text = value.toString();
        if (text.size() > previewLength) {
            return text.left(previewLength)
                + QStringLiteral("…（完整值共 %1 个字符）").arg(text.size());
        }
    } else if (value.metaType().id() == QMetaType::QByteArray) {
        return QStringLiteral("二进制数据（%1 字节）").arg(value.toByteArray().size());
    }
    return value;
}

QVariant ResultTableModel::normalizedValue(const QModelIndex &index,
                                           const QVariant &value) const
{
    const QVariant original = result_.rows.at(index.row()).at(index.column());
    const QString rawText = value.toString();
    const QString trimmedText = rawText.trimmed();
    if (original.isNull() && (value.isNull() || rawText.isEmpty()))
        return original;
    if (trimmedText.compare(QStringLiteral("NULL"), Qt::CaseInsensitive) == 0)
        return QVariant(original.metaType());

    bool ok = false;
    switch (original.metaType().id()) {
    case QMetaType::QString:
        return rawText;
    case QMetaType::Int: {
        const int number = trimmedText.toInt(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    case QMetaType::UInt: {
        const uint number = trimmedText.toUInt(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    case QMetaType::LongLong: {
        const qlonglong number = trimmedText.toLongLong(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    case QMetaType::ULongLong: {
        const qulonglong number = trimmedText.toULongLong(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    case QMetaType::Double: {
        const double number = trimmedText.toDouble(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    case QMetaType::Float: {
        const float number = trimmedText.toFloat(&ok);
        return ok ? QVariant(number) : QVariant{};
    }
    case QMetaType::Bool:
        if (trimmedText.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
            || trimmedText == QStringLiteral("1"))
            return true;
        if (trimmedText.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0
            || trimmedText == QStringLiteral("0"))
            return false;
        return {};
    case QMetaType::QDate: {
        const QDate date = QDate::fromString(trimmedText, Qt::ISODate);
        return date.isValid() ? QVariant(date) : QVariant{};
    }
    case QMetaType::QTime: {
        const QTime time = QTime::fromString(trimmedText, Qt::ISODateWithMs);
        return time.isValid() ? QVariant(time) : QVariant{};
    }
    case QMetaType::QDateTime: {
        const QDateTime dateTime = QDateTime::fromString(trimmedText, Qt::ISODateWithMs);
        return dateTime.isValid() ? QVariant(dateTime) : QVariant{};
    }
    case QMetaType::QUuid: {
        const QUuid uuid = QUuid::fromString(trimmedText);
        if (!uuid.isNull()
            || trimmedText == QStringLiteral("00000000-0000-0000-0000-000000000000")
            || trimmedText == QStringLiteral("{00000000-0000-0000-0000-000000000000}"))
            return uuid;
        return {};
    }
    case QMetaType::QByteArray:
        return value.metaType().id() == QMetaType::QByteArray
            ? value : QVariant(rawText.toUtf8());
    default: {
        if (value.metaType() == original.metaType())
            return value;
        QVariant converted = value;
        return converted.convert(original.metaType()) ? converted : QVariant{};
    }
    }
}

void ResultTableModel::notifyChangedCells(const QList<quint64> &keys)
{
    for (const quint64 key : keys) {
        const int row = keyRow(key);
        const int column = keyColumn(key);
        if (row < result_.rows.size() && column < result_.columns.size())
            emit dataChanged(index(row, column), index(row, column),
                             {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole,
                              Qt::ForegroundRole, Qt::ToolTipRole});
    }
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

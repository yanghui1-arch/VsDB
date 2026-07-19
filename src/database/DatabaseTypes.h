#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace vsdb {

struct PostgresConnectionConfig
{
    QString host = QStringLiteral("localhost");
    int port = 5432;
    QString user;
    QString password;
    QString database = QStringLiteral("postgres");
    QString sslMode = QStringLiteral("prefer");
    int connectTimeoutSeconds = 10;

    QString displayName() const
    {
        return QStringLiteral("%1@%2:%3/%4").arg(user, host).arg(port).arg(database);
    }
};

struct DatabaseRelation
{
    QString schema;
    QString name;
    bool view = false;
};

struct DatabaseColumn
{
    QString name;
    QString type;
    bool nullable = true;
    QString defaultValue;
    bool primaryKey = false;
};

struct DatabaseIndex
{
    QString name;
    QString definition;
};

struct DatabaseForeignKey
{
    QString name;
    QString definition;
};

struct DatabaseTable
{
    QString schema;
    QString name;
    qlonglong estimatedRows = -1;
    QString totalSize;
    QString description;
    QVector<DatabaseColumn> columns;
    QVector<DatabaseIndex> indexes;
    QVector<DatabaseForeignKey> foreignKeys;

    QStringList primaryKeys() const
    {
        QStringList keys;
        for (const DatabaseColumn &column : columns) {
            if (column.primaryKey)
                keys.append(column.name);
        }
        return keys;
    }
};

struct QueryColumn
{
    QString name;
    QString type;
};

struct QueryResult
{
    QVector<QueryColumn> columns;
    QVector<QVariantList> rows;
    qlonglong affectedRows = -1;
    bool select = false;
    bool truncated = false;
};

struct CellChange
{
    int row = -1;
    int column = -1;
    QVariant originalValue;
    QVariant value;
};

} // namespace vsdb

#include "database/postgres/PostgresSession.h"

#include <QMap>
#include <QMetaType>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace vsdb {

namespace {

void assignError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

void clearError(QString *target)
{
    if (target)
        target->clear();
}

QString sqlTypeName(const QMetaType &type)
{
    switch (type.id()) {
    case QMetaType::Int: return QStringLiteral("integer");
    case QMetaType::UInt: return QStringLiteral("unsigned integer");
    case QMetaType::LongLong: return QStringLiteral("bigint");
    case QMetaType::ULongLong: return QStringLiteral("unsigned bigint");
    case QMetaType::Double:
    case QMetaType::Float: return QStringLiteral("numeric");
    case QMetaType::Bool: return QStringLiteral("boolean");
    case QMetaType::QDate: return QStringLiteral("date");
    case QMetaType::QTime: return QStringLiteral("time");
    case QMetaType::QDateTime: return QStringLiteral("timestamp");
    case QMetaType::QByteArray: return QStringLiteral("bytea");
    case QMetaType::QUuid: return QStringLiteral("uuid");
    case QMetaType::QString: return QStringLiteral("text");
    default: {
        const char *name = type.name();
        return name ? QString::fromLatin1(name) : QStringLiteral("unknown");
    }
    }
}

QString queryError(const QSqlQuery &query)
{
    const QSqlError error = query.lastError();
    const QString databaseText = error.databaseText().trimmed();
    return databaseText.isEmpty() ? error.text().trimmed() : databaseText;
}

} // namespace

PostgresSession::PostgresSession()
    : connectionName_(QStringLiteral("vsdb-postgres-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

PostgresSession::~PostgresSession()
{
    disconnect();
    if (QSqlDatabase::contains(connectionName_))
        QSqlDatabase::removeDatabase(connectionName_);
}

bool PostgresSession::connectToServer(const PostgresConnectionConfig &config,
                                      QString *error)
{
    clearError(error);
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QPSQL"))) {
        assignError(error, QStringLiteral(
            "未找到 PostgreSQL 驱动（QPSQL）。请安装 Qt SQL PostgreSQL 驱动并确保 libpq 可用。"));
        return false;
    }

    if (!QSqlDatabase::contains(connectionName_))
        QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), connectionName_);

    const bool previousConnected = isConnected();
    const PostgresConnectionConfig previousConfig = config_;
    const QString previousUser = currentUser_;
    const bool previousConfigured = configured_;
    config_ = config;
    configured_ = true;
    currentUser_ = config.user;
    if (openDatabase(config.database, error))
        return true;

    const QString connectionFailure = error ? *error : QString{};
    if (previousConnected) {
        config_ = previousConfig;
        currentUser_ = previousUser;
        configured_ = previousConfigured;
        QString restoreError;
        if (!openDatabase(previousConfig.database, &restoreError)) {
            assignError(error, connectionFailure
                + QStringLiteral("\n恢复原连接也失败：%1").arg(restoreError));
            return false;
        }
    }
    assignError(error, connectionFailure);
    return false;
}

bool PostgresSession::reconnect(QString *error)
{
    clearError(error);
    if (!configured_) {
        assignError(error, QStringLiteral("尚未配置 PostgreSQL 连接。"));
        return false;
    }
    return openDatabase(config_.database, error);
}

void PostgresSession::disconnect()
{
    if (!QSqlDatabase::contains(connectionName_))
        return;
    {
        QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
        database.close();
    }
}

bool PostgresSession::isConnected() const
{
    return QSqlDatabase::contains(connectionName_)
        && QSqlDatabase::database(connectionName_, false).isOpen();
}

const PostgresConnectionConfig &PostgresSession::config() const
{
    return config_;
}

QString PostgresSession::currentUser() const
{
    return currentUser_;
}

QString PostgresSession::serverVersion() const
{
    if (!isConnected())
        return {};
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral("SELECT current_setting('server_version')")) || !query.next())
        return {};
    return query.value(0).toString();
}

bool PostgresSession::openDatabase(const QString &databaseName, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
    database.close();
    database.setHostName(config_.host);
    database.setPort(config_.port);
    database.setDatabaseName(databaseName);
    database.setUserName(config_.user);
    database.setConnectOptions(
        QStringLiteral("connect_timeout=%1;sslmode=%2;application_name=VsDB")
            .arg(qBound(1, config_.connectTimeoutSeconds, 60))
            .arg(config_.sslMode));

    if (!database.open(config_.user, config_.password)) {
        assignError(error, databaseError());
        return false;
    }

    config_.database = databaseName;
    const QString requestedUser = currentUser_.isEmpty() ? config_.user : currentUser_;
    currentUser_ = config_.user;
    if (requestedUser != config_.user && !setSessionUser(requestedUser, error)) {
        database.close();
        return false;
    }
    return true;
}

bool PostgresSession::switchUser(const QString &user, QString *error)
{
    clearError(error);
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return false;
    }
    return setSessionUser(user, error);
}

bool PostgresSession::setSessionUser(const QString &user, QString *error)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (!query.exec(QStringLiteral("RESET SESSION AUTHORIZATION"))) {
        assignError(error, queryError(query));
        return false;
    }
    currentUser_ = config_.user;

    if (user == config_.user)
        return true;
    if (!query.exec(QStringLiteral("SET SESSION AUTHORIZATION %1").arg(quoteIdentifier(user)))) {
        assignError(error, queryError(query));
        return false;
    }
    currentUser_ = user;
    return true;
}

bool PostgresSession::switchDatabase(const QString &databaseName, QString *error)
{
    clearError(error);
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return false;
    }

    const QString previousDatabase = config_.database;
    if (openDatabase(databaseName, error))
        return true;

    QString restoreError;
    if (!openDatabase(previousDatabase, &restoreError) && error)
        *error += QStringLiteral("\n恢复原数据库连接也失败：%1").arg(restoreError);
    return false;
}

QStringList PostgresSession::users(QString *error) const
{
    clearError(error);
    QStringList result;
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT rolname FROM pg_catalog.pg_roles "
            "WHERE rolcanlogin ORDER BY rolname"))) {
        assignError(error, queryError(query));
        return result;
    }
    while (query.next())
        result.append(query.value(0).toString());
    if (query.lastError().isValid())
        assignError(error, queryError(query));
    return result;
}

QStringList PostgresSession::databases(QString *error) const
{
    clearError(error);
    QStringList result;
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT datname FROM pg_catalog.pg_database "
            "WHERE datallowconn AND NOT datistemplate ORDER BY datname"))) {
        assignError(error, queryError(query));
        return result;
    }
    while (query.next())
        result.append(query.value(0).toString());
    if (query.lastError().isValid())
        assignError(error, queryError(query));
    return result;
}

QStringList PostgresSession::schemas(QString *error) const
{
    clearError(error);
    QStringList result;
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT schema_name FROM information_schema.schemata "
            "WHERE schema_name NOT LIKE 'pg_toast%' "
            "AND schema_name NOT LIKE 'pg_temp_%' ORDER BY schema_name"))) {
        assignError(error, queryError(query));
        return result;
    }
    while (query.next())
        result.append(query.value(0).toString());
    if (query.lastError().isValid())
        assignError(error, queryError(query));
    return result;
}

QVector<DatabaseRelation> PostgresSession::relations(const QString &schema,
                                                     QString *error) const
{
    clearError(error);
    QVector<DatabaseRelation> result;
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
        "SELECT table_name, table_type FROM information_schema.tables "
        "WHERE table_schema = :schema ORDER BY table_type, table_name"));
    query.bindValue(QStringLiteral(":schema"), schema);
    if (!query.exec()) {
        assignError(error, queryError(query));
        return result;
    }
    while (query.next()) {
        DatabaseRelation relation;
        relation.schema = schema;
        relation.name = query.value(0).toString();
        relation.view = query.value(1).toString() == QStringLiteral("VIEW");
        result.append(relation);
    }
    if (query.lastError().isValid())
        assignError(error, queryError(query));
    return result;
}

DatabaseTable PostgresSession::describeTable(const QString &schema,
                                             const QString &table,
                                             QString *error) const
{
    clearError(error);
    DatabaseTable result;
    result.schema = schema;
    result.name = table;
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return result;
    }
    QSqlDatabase database = QSqlDatabase::database(connectionName_);

    QSqlQuery summary(database);
    summary.setForwardOnly(true);
    summary.prepare(QStringLiteral(
        "SELECT c.reltuples::bigint, "
        "pg_catalog.pg_size_pretty(pg_catalog.pg_total_relation_size(c.oid)), "
        "COALESCE(pg_catalog.obj_description(c.oid, 'pg_class'), '') "
        "FROM pg_catalog.pg_class c "
        "JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
        "WHERE n.nspname = :schema AND c.relname = :table"));
    summary.bindValue(QStringLiteral(":schema"), schema);
    summary.bindValue(QStringLiteral(":table"), table);
    if (!summary.exec() || !summary.next()) {
        assignError(error, summary.lastError().isValid()
                              ? queryError(summary)
                              : QStringLiteral("找不到关系 %1.%2").arg(schema, table));
        return result;
    }
    result.estimatedRows = summary.value(0).toLongLong();
    result.totalSize = summary.value(1).toString();
    result.description = summary.value(2).toString();
    summary.finish();

    QSqlQuery columns(database);
    columns.setForwardOnly(true);
    columns.prepare(QStringLiteral(
        "SELECT a.attname, pg_catalog.format_type(a.atttypid, a.atttypmod), "
        "NOT a.attnotnull, COALESCE(pg_catalog.pg_get_expr(ad.adbin, ad.adrelid), ''), "
        "EXISTS (SELECT 1 FROM pg_catalog.pg_index i "
        "        WHERE i.indrelid = c.oid AND i.indisprimary AND a.attnum = ANY(i.indkey)) "
        "FROM pg_catalog.pg_attribute a "
        "JOIN pg_catalog.pg_class c ON c.oid = a.attrelid "
        "JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
        "LEFT JOIN pg_catalog.pg_attrdef ad ON ad.adrelid = c.oid AND ad.adnum = a.attnum "
        "WHERE n.nspname = :schema AND c.relname = :table "
        "AND a.attnum > 0 AND NOT a.attisdropped ORDER BY a.attnum"));
    columns.bindValue(QStringLiteral(":schema"), schema);
    columns.bindValue(QStringLiteral(":table"), table);
    if (!columns.exec()) {
        assignError(error, queryError(columns));
        return result;
    }
    while (columns.next()) {
        result.columns.append({columns.value(0).toString(), columns.value(1).toString(),
                               columns.value(2).toBool(), columns.value(3).toString(),
                               columns.value(4).toBool()});
    }
    if (columns.lastError().isValid()) {
        assignError(error, queryError(columns));
        return result;
    }

    QSqlQuery indexes(database);
    indexes.setForwardOnly(true);
    indexes.prepare(QStringLiteral(
        "SELECT indexname, indexdef FROM pg_catalog.pg_indexes "
        "WHERE schemaname = :schema AND tablename = :table ORDER BY indexname"));
    indexes.bindValue(QStringLiteral(":schema"), schema);
    indexes.bindValue(QStringLiteral(":table"), table);
    if (!indexes.exec()) {
        assignError(error, queryError(indexes));
        return result;
    }
    while (indexes.next())
        result.indexes.append({indexes.value(0).toString(), indexes.value(1).toString()});
    if (indexes.lastError().isValid()) {
        assignError(error, queryError(indexes));
        return result;
    }

    QSqlQuery foreignKeys(database);
    foreignKeys.setForwardOnly(true);
    foreignKeys.prepare(QStringLiteral(
        "SELECT con.conname, pg_catalog.pg_get_constraintdef(con.oid, true) "
        "FROM pg_catalog.pg_constraint con "
        "JOIN pg_catalog.pg_class c ON c.oid = con.conrelid "
        "JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
        "WHERE con.contype = 'f' AND n.nspname = :schema AND c.relname = :table "
        "ORDER BY con.conname"));
    foreignKeys.bindValue(QStringLiteral(":schema"), schema);
    foreignKeys.bindValue(QStringLiteral(":table"), table);
    if (!foreignKeys.exec()) {
        assignError(error, queryError(foreignKeys));
        return result;
    }
    while (foreignKeys.next())
        result.foreignKeys.append({foreignKeys.value(0).toString(), foreignKeys.value(1).toString()});
    if (foreignKeys.lastError().isValid())
        assignError(error, queryError(foreignKeys));
    return result;
}

QString PostgresSession::buildRelationPreview(const DatabaseTable &table,
                                              int rowLimit) const
{
    QStringList selectedColumns;
    selectedColumns.reserve(table.columns.size());
    for (const DatabaseColumn &column : table.columns)
        selectedColumns.append(quoteIdentifier(column.name));
    if (selectedColumns.isEmpty())
        selectedColumns.append(QStringLiteral("*"));

    return QStringLiteral("SELECT\n    %1\nFROM %2\nLIMIT %3;")
        .arg(selectedColumns.join(QStringLiteral(",\n    ")),
             qualifiedName(table.schema, table.name))
        .arg(qBound(1, rowLimit, 10000));
}

QueryResult PostgresSession::execute(const QString &sql, int rowLimit,
                                     QString *error) const
{
    clearError(error);
    QueryResult result;
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return result;
    }

    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    if (!query.exec(sql)) {
        assignError(error, queryError(query));
        return result;
    }

    result.select = query.isSelect();
    if (!result.select) {
        result.affectedRows = query.numRowsAffected();
        return result;
    }

    const QSqlRecord record = query.record();
    result.columns.reserve(record.count());
    for (int column = 0; column < record.count(); ++column) {
        const QSqlField field = record.field(column);
        result.columns.append({field.name(), sqlTypeName(field.metaType())});
    }

    const int boundedLimit = qBound(1, rowLimit, 10000);
    result.rows.reserve(boundedLimit);
    while (result.rows.size() < boundedLimit && query.next()) {
        QVariantList row;
        row.reserve(record.count());
        for (int column = 0; column < record.count(); ++column)
            row.append(query.value(column));
        result.rows.append(row);
    }
    if (result.rows.size() == boundedLimit && query.next())
        result.truncated = true;
    if (query.lastError().isValid())
        assignError(error, queryError(query));
    return result;
}

bool PostgresSession::cancelBackend(qint64 backendPid, QString *error) const
{
    clearError(error);
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return false;
    }
    if (backendPid <= 0) {
        assignError(error, QStringLiteral("查询后端尚未就绪。"));
        return false;
    }

    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT pg_catalog.pg_cancel_backend(:backend_pid)"));
    query.bindValue(QStringLiteral(":backend_pid"), backendPid);
    if (!query.exec() || !query.next()) {
        assignError(error, queryError(query));
        return false;
    }
    return query.value(0).toBool();
}

bool PostgresSession::applyChanges(const QString &schema, const QString &table,
                                   const QVector<QueryColumn> &columns,
                                   const QVector<QVariantList> &originalRows,
                                   const QStringList &primaryKeys,
                                   const QVector<CellChange> &changes,
                                   QString *error) const
{
    clearError(error);
    if (!isConnected()) {
        assignError(error, QStringLiteral("PostgreSQL 尚未连接。"));
        return false;
    }
    if (primaryKeys.isEmpty()) {
        assignError(error, QStringLiteral("该表没有主键，无法安全定位需要更新的记录。"));
        return false;
    }

    QMap<int, QMap<int, QVariant>> rows;
    for (const CellChange &change : changes)
        rows[change.row].insert(change.column, change.value);
    if (rows.isEmpty())
        return true;

    QMap<QString, int> columnIndexes;
    for (int index = 0; index < columns.size(); ++index)
        columnIndexes.insert(columns.at(index).name, index);
    for (const QString &key : primaryKeys) {
        if (!columnIndexes.contains(key)) {
            assignError(error, QStringLiteral("查询结果未包含主键列 %1，无法安全更新。").arg(key));
            return false;
        }
    }

    QSqlDatabase database = QSqlDatabase::database(connectionName_);
    if (!database.transaction()) {
        assignError(error, databaseError());
        return false;
    }

    for (auto row = rows.constBegin(); row != rows.constEnd(); ++row) {
        if (row.key() < 0 || row.key() >= originalRows.size()) {
            database.rollback();
            assignError(error, QStringLiteral("结果行已经失效，请重新查询后再编辑。"));
            return false;
        }

        QStringList assignments;
        int valueIndex = 0;
        for (auto value = row.value().constBegin(); value != row.value().constEnd(); ++value) {
            if (value.key() < 0 || value.key() >= columns.size()) {
                database.rollback();
                assignError(error, QStringLiteral("结果列已经失效，请重新查询后再编辑。"));
                return false;
            }
            assignments.append(QStringLiteral("%1 = :value%2")
                                   .arg(quoteIdentifier(columns.at(value.key()).name))
                                   .arg(valueIndex++));
        }

        QStringList predicates;
        for (int keyIndex = 0; keyIndex < primaryKeys.size(); ++keyIndex) {
            predicates.append(QStringLiteral("%1 IS NOT DISTINCT FROM :key%2")
                                  .arg(quoteIdentifier(primaryKeys.at(keyIndex)))
                                  .arg(keyIndex));
        }

        QSqlQuery query(database);
        const QString sql = QStringLiteral("UPDATE %1 SET %2 WHERE %3")
                                .arg(qualifiedName(schema, table), assignments.join(QStringLiteral(", ")),
                                     predicates.join(QStringLiteral(" AND ")));
        if (!query.prepare(sql)) {
            database.rollback();
            assignError(error, queryError(query));
            return false;
        }
        valueIndex = 0;
        for (auto value = row.value().constBegin(); value != row.value().constEnd(); ++value)
            query.bindValue(QStringLiteral(":value%1").arg(valueIndex++), value.value());
        const QVariantList &original = originalRows.at(row.key());
        for (int keyIndex = 0; keyIndex < primaryKeys.size(); ++keyIndex)
            query.bindValue(QStringLiteral(":key%1").arg(keyIndex),
                            original.at(columnIndexes.value(primaryKeys.at(keyIndex))));

        if (!query.exec() || query.numRowsAffected() != 1) {
            const QString message = query.lastError().isValid()
                ? queryError(query)
                : QStringLiteral("记录已被修改或删除，本次更新未命中唯一记录。");
            database.rollback();
            assignError(error, message);
            return false;
        }
    }

    if (!database.commit()) {
        const QString message = databaseError();
        database.rollback();
        assignError(error, message);
        return false;
    }
    return true;
}

QString PostgresSession::quoteIdentifier(const QString &identifier) const
{
    if (!QSqlDatabase::contains(connectionName_)) {
        QString escaped = identifier;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
    return database.driver()->escapeIdentifier(identifier, QSqlDriver::FieldName);
}

QString PostgresSession::qualifiedName(const QString &schema, const QString &relation) const
{
    return quoteIdentifier(schema) + QLatin1Char('.') + quoteIdentifier(relation);
}

QString PostgresSession::databaseError() const
{
    if (!QSqlDatabase::contains(connectionName_))
        return QStringLiteral("PostgreSQL 连接不存在。");
    const QSqlError error = QSqlDatabase::database(connectionName_, false).lastError();
    const QString databaseText = error.databaseText().trimmed();
    return databaseText.isEmpty() ? error.text().trimmed() : databaseText;
}

} // namespace vsdb

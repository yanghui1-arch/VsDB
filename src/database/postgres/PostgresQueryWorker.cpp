#include "database/postgres/PostgresQueryWorker.h"

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

QString sqlError(const QSqlError &error)
{
    const QString databaseText = error.databaseText().trimmed();
    return databaseText.isEmpty() ? error.text().trimmed() : databaseText;
}

QString queryError(const QSqlQuery &query)
{
    return sqlError(query.lastError());
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

bool sameConfig(const PostgresConnectionConfig &left,
                const PostgresConnectionConfig &right)
{
    return left.host == right.host
        && left.port == right.port
        && left.user == right.user
        && left.password == right.password
        && left.database == right.database
        && left.sslMode == right.sslMode
        && left.connectTimeoutSeconds == right.connectTimeoutSeconds;
}

qint64 estimatedBytes(const QVariant &value)
{
    constexpr qint64 variantOverhead = 32;
    switch (value.metaType().id()) {
    case QMetaType::QString:
        return variantOverhead + value.toString().size() * qint64(sizeof(QChar));
    case QMetaType::QByteArray:
        return variantOverhead + value.toByteArray().size();
    default:
        return variantOverhead;
    }
}

} // namespace

PostgresQueryWorker::PostgresQueryWorker(QObject *parent)
    : QObject(parent),
      connectionName_(QStringLiteral("vsdb-query-worker-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

PostgresQueryWorker::~PostgresQueryWorker()
{
    closeConnection();
}

void PostgresQueryWorker::prepareRequest()
{
    cancelRequested_.store(false, std::memory_order_release);
}

void PostgresQueryWorker::requestCancel()
{
    cancelRequested_.store(true, std::memory_order_release);
}

void PostgresQueryWorker::execute(PostgresQueryRequest request)
{
    QString error;
    if (cancelRequested_.load(std::memory_order_acquire)) {
        emit finished(request.id, -1, false, false, true, 0, {});
        return;
    }
    if (!ensureConnection(request, &error)) {
        emit finished(request.id, -1, false, false, false, 0, error);
        return;
    }

    QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
    {
        QSqlQuery backend(database);
        backend.setForwardOnly(true);
        if (backend.exec(QStringLiteral("SELECT pg_catalog.pg_backend_pid()")) && backend.next())
            emit backendReady(request.id, backend.value(0).toLongLong());
    }

    if (cancelRequested_.load(std::memory_order_acquire)) {
        emit finished(request.id, -1, false, false, true, 0, {});
        return;
    }

    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.exec(request.sql)) {
        const bool cancelled = cancelRequested_.load(std::memory_order_acquire);
        emit finished(request.id, -1, false, false, cancelled, 0,
                      cancelled ? QString{} : queryError(query));
        return;
    }

    const bool select = query.isSelect();
    QVector<QueryColumn> columns;
    QSqlRecord record;
    if (select) {
        record = query.record();
        columns.reserve(record.count());
        for (int column = 0; column < record.count(); ++column) {
            const QSqlField field = record.field(column);
            columns.append({field.name(), sqlTypeName(field.metaType())});
        }
    }
    emit resultSetReady(request.id, columns, select);

    if (!select) {
        emit finished(request.id, query.numRowsAffected(), false, false, false, 0, {});
        return;
    }

    const int rowLimit = qBound(1, request.rowLimit, 10000);
    const int batchSize = qBound(1, request.batchSize, 256);
    const qint64 memoryLimit = qBound<qint64>(1024 * 1024, request.memoryLimitBytes,
                                             1024LL * 1024 * 1024);
    QVector<QVariantList> batch;
    batch.reserve(qMin(batchSize, rowLimit));
    int rowCount = 0;
    qint64 loadedBytes = 0;
    bool memoryLimited = false;
    bool cancelled = false;

    while (rowCount < rowLimit && query.next()) {
        if (cancelRequested_.load(std::memory_order_acquire)) {
            cancelled = true;
            break;
        }

        QVariantList row;
        row.reserve(record.count());
        qint64 rowBytes = qint64(record.count()) * qint64(sizeof(QVariant));
        for (int column = 0; column < record.count(); ++column) {
            QVariant value = query.value(column);
            rowBytes += estimatedBytes(value);
            row.append(std::move(value));
        }

        if (loadedBytes + rowBytes > memoryLimit) {
            memoryLimited = true;
            break;
        }

        loadedBytes += rowBytes;
        batch.append(std::move(row));
        ++rowCount;
        if (batch.size() >= batchSize) {
            emit rowsReady(request.id, std::move(batch), loadedBytes);
            batch = {};
            batch.reserve(qMin(batchSize, rowLimit - rowCount));
        }
    }

    if (!batch.isEmpty())
        emit rowsReady(request.id, std::move(batch), loadedBytes);

    bool truncated = memoryLimited;
    if (!cancelled && !memoryLimited && rowCount == rowLimit && query.next())
        truncated = true;
    if (!cancelled && query.lastError().isValid())
        error = queryError(query);
    cancelled = cancelled || cancelRequested_.load(std::memory_order_acquire);
    emit finished(request.id, -1, truncated, memoryLimited, cancelled,
                  loadedBytes, cancelled ? QString{} : error);
}

bool PostgresQueryWorker::ensureConnection(const PostgresQueryRequest &request,
                                           QString *error)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QPSQL"))) {
        *error = QStringLiteral(
            "未找到 PostgreSQL 驱动（QPSQL）。请安装 Qt SQL PostgreSQL 驱动并确保 libpq 可用。");
        return false;
    }

    const bool reusable = configured_ && sameConfig(activeConfig_, request.config)
        && QSqlDatabase::contains(connectionName_)
        && QSqlDatabase::database(connectionName_, false).isOpen();
    if (!reusable) {
        closeConnection();
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), connectionName_);
        database.setHostName(request.config.host);
        database.setPort(request.config.port);
        database.setDatabaseName(request.config.database);
        database.setUserName(request.config.user);
        database.setConnectOptions(
            QStringLiteral("connect_timeout=%1;sslmode=%2;application_name=VsDB-query")
                .arg(qBound(1, request.config.connectTimeoutSeconds, 60))
                .arg(request.config.sslMode));
        if (!database.open(request.config.user, request.config.password)) {
            *error = sqlError(database.lastError());
            database = QSqlDatabase{};
            QSqlDatabase::removeDatabase(connectionName_);
            configured_ = false;
            return false;
        }
        activeConfig_ = request.config;
        activeSessionUser_ = request.config.user;
        configured_ = true;
    }

    return applySessionUser(request.sessionUser.isEmpty() ? request.config.user
                                                           : request.sessionUser,
                            error);
}

bool PostgresQueryWorker::applySessionUser(const QString &user, QString *error)
{
    if (user == activeSessionUser_)
        return true;

    QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("RESET SESSION AUTHORIZATION"))) {
        *error = queryError(query);
        return false;
    }
    activeSessionUser_ = activeConfig_.user;
    if (user == activeConfig_.user)
        return true;

    const QString escaped = database.driver()->escapeIdentifier(user, QSqlDriver::FieldName);
    if (!query.exec(QStringLiteral("SET SESSION AUTHORIZATION %1").arg(escaped))) {
        *error = queryError(query);
        return false;
    }
    activeSessionUser_ = user;
    return true;
}

void PostgresQueryWorker::closeConnection()
{
    if (!QSqlDatabase::contains(connectionName_)) {
        configured_ = false;
        return;
    }
    {
        QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName_);
    configured_ = false;
    activeSessionUser_.clear();
}

} // namespace vsdb

#pragma once

#include "database/DatabaseTypes.h"

#include <QObject>

#include <atomic>

namespace vsdb {

struct PostgresQueryRequest
{
    quint64 id = 0;
    PostgresConnectionConfig config;
    QString sessionUser;
    QString sql;
    int rowLimit = 100;
    int batchSize = 32;
    qint64 memoryLimitBytes = 64 * 1024 * 1024;
};

class PostgresQueryWorker final : public QObject
{
    Q_OBJECT

public:
    explicit PostgresQueryWorker(QObject *parent = nullptr);
    ~PostgresQueryWorker() override;

    void prepareRequest();
    void requestCancel();

public slots:
    void execute(PostgresQueryRequest request);

signals:
    void backendReady(quint64 requestId, qint64 backendPid);
    void resultSetReady(quint64 requestId, QVector<QueryColumn> columns, bool select);
    void rowsReady(quint64 requestId, QVector<QVariantList> rows, qint64 loadedBytes);
    void finished(quint64 requestId, qlonglong affectedRows, bool truncated,
                  bool memoryLimited, bool cancelled, qint64 loadedBytes,
                  const QString &error);

private:
    bool ensureConnection(const PostgresQueryRequest &request, QString *error);
    bool applySessionUser(const QString &user, QString *error);
    void closeConnection();

    QString connectionName_;
    PostgresConnectionConfig activeConfig_;
    QString activeSessionUser_;
    bool configured_ = false;
    std::atomic_bool cancelRequested_ = false;
};

} // namespace vsdb

Q_DECLARE_METATYPE(vsdb::PostgresQueryRequest)

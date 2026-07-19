#include "database/postgres/PostgresSession.h"
#include "database/postgres/PostgresQueryWorker.h"

#include <QCoreApplication>
#include <iostream>
#include <utility>

namespace {

int fail(int code, const QString &message)
{
    std::cerr << message.toStdString() << '\n';
    return code;
}

bool exec(vsdb::PostgresSession &session, const QString &sql, QString *error)
{
    session.execute(sql, 100, error);
    return error->isEmpty();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    vsdb::PostgresSession session;
    vsdb::DatabaseTable previewTable;
    previewTable.schema = QStringLiteral("public");
    previewTable.name = QStringLiteral("events");
    previewTable.columns = {
        {QStringLiteral("id"), QStringLiteral("uuid"), false, {}, true},
        {QStringLiteral("summary"), QStringLiteral("character varying(200)"), true, {}, false},
        {QStringLiteral("payload"), QStringLiteral("text"), true, {}, false},
        {QStringLiteral("document"), QStringLiteral("jsonb"), true, {}, false}
    };
    const QString preview = session.buildRelationPreview(previewTable, 50);
    const qsizetype idPosition = preview.indexOf(QStringLiteral("\"id\""));
    const qsizetype summaryPosition = preview.indexOf(QStringLiteral("\"summary\""));
    const qsizetype payloadPosition = preview.indexOf(QStringLiteral("\"payload\""));
    const qsizetype documentPosition = preview.indexOf(QStringLiteral("\"document\""));
    if (idPosition < 0 || summaryPosition <= idPosition || payloadPosition <= summaryPosition
        || documentPosition <= payloadPosition
        || !preview.endsWith(QStringLiteral("LIMIT 50;")))
        return fail(21, QStringLiteral("table preview did not preserve all columns"));

    const QString host = qEnvironmentVariable("VSDB_TEST_PG_HOST");
    if (host.isEmpty()) {
        std::cout << "VSDB_TEST_PG_HOST is not set; integration test skipped\n";
        return 0;
    }

    vsdb::PostgresConnectionConfig config;
    config.host = host;
    config.port = qEnvironmentVariableIntValue("VSDB_TEST_PG_PORT");
    if (config.port <= 0)
        config.port = 5432;
    config.user = qEnvironmentVariable("VSDB_TEST_PG_USER", "postgres");
    config.password = qEnvironmentVariable("VSDB_TEST_PG_PASSWORD");
    config.database = qEnvironmentVariable("VSDB_TEST_PG_DATABASE", "postgres");
    config.sslMode = qEnvironmentVariable("VSDB_TEST_PG_SSLMODE", "disable");

    QString error;
    if (!session.connectToServer(config, &error))
        return fail(1, QStringLiteral("connect failed: %1").arg(error));

    vsdb::PostgresQueryWorker worker;
    QVector<vsdb::QueryColumn> streamedColumns;
    QVector<QVariantList> streamedRows;
    QString workerError;
    bool workerFinished = false;
    QObject::connect(&worker, &vsdb::PostgresQueryWorker::resultSetReady,
                     [&](quint64, QVector<vsdb::QueryColumn> columns, bool) {
        streamedColumns = std::move(columns);
    });
    QObject::connect(&worker, &vsdb::PostgresQueryWorker::rowsReady,
                     [&](quint64, QVector<QVariantList> rows, qint64) {
        for (QVariantList &row : rows)
            streamedRows.append(std::move(row));
    });
    QObject::connect(&worker, &vsdb::PostgresQueryWorker::finished,
                     [&](quint64, qlonglong, bool, bool, bool, qint64,
                         const QString &queryError) {
        workerFinished = true;
        workerError = queryError;
    });
    vsdb::PostgresQueryRequest request;
    request.id = 1;
    request.config = config;
    request.sessionUser = config.user;
    request.sql = QStringLiteral("SELECT value FROM generate_series(1, 3) AS value");
    request.rowLimit = 10;
    request.batchSize = 2;
    worker.prepareRequest();
    worker.execute(request);
    if (!workerFinished || !workerError.isEmpty() || streamedColumns.size() != 1
        || streamedRows.size() != 3 || streamedRows.constLast().constFirst().toInt() != 3)
        return fail(22, QStringLiteral("streaming query worker failed: %1").arg(workerError));

    if (!session.users(&error).contains(config.user) || !error.isEmpty())
        return fail(2, QStringLiteral("users failed: %1").arg(error));
    if (!session.databases(&error).contains(config.database) || !error.isEmpty())
        return fail(3, QStringLiteral("databases failed: %1").arg(error));

    const QString schema = QStringLiteral("vsdb_integration");
    if (!exec(session, QStringLiteral("DROP SCHEMA IF EXISTS %1 CASCADE")
                           .arg(session.quoteIdentifier(schema)), &error)
        || !exec(session, QStringLiteral("CREATE SCHEMA %1")
                              .arg(session.quoteIdentifier(schema)), &error)
        || !exec(session, QStringLiteral(
                    "CREATE TABLE %1 (id bigint PRIMARY KEY, name text NOT NULL, active boolean DEFAULT true)")
                              .arg(session.qualifiedName(schema, QStringLiteral("items"))), &error))
        return fail(4, QStringLiteral("fixture setup failed: %1").arg(error));

    vsdb::QueryResult insert = session.execute(
        QStringLiteral("INSERT INTO %1 (id, name) VALUES (1, 'alpha'), (2, 'beta')")
            .arg(session.qualifiedName(schema, QStringLiteral("items"))), 100, &error);
    if (!error.isEmpty() || insert.affectedRows != 2)
        return fail(5, QStringLiteral("insert failed: %1").arg(error));

    const QVector<vsdb::DatabaseRelation> relations = session.relations(schema, &error);
    if (!error.isEmpty() || relations.size() != 1 || relations.constFirst().name != QStringLiteral("items"))
        return fail(6, QStringLiteral("relation discovery failed: %1").arg(error));
    const vsdb::DatabaseTable table = session.describeTable(schema, QStringLiteral("items"), &error);
    if (!error.isEmpty() || table.columns.size() != 3
        || table.primaryKeys() != QStringList{QStringLiteral("id")})
        return fail(7, QStringLiteral("table metadata failed: %1").arg(error));

    vsdb::QueryResult select = session.execute(
        QStringLiteral("SELECT id, name, active FROM %1 ORDER BY id")
            .arg(session.qualifiedName(schema, QStringLiteral("items"))), 100, &error);
    if (!error.isEmpty() || !select.select || select.rows.size() != 2
        || select.rows.constFirst().at(1).toString() != QStringLiteral("alpha"))
        return fail(8, QStringLiteral("select failed: %1").arg(error));

    const QVector<vsdb::CellChange> changes{{0, 1, QStringLiteral("alpha"), QStringLiteral("updated")}};
    if (!session.applyChanges(schema, QStringLiteral("items"), select.columns, select.rows,
                              table.primaryKeys(), changes, &error))
        return fail(9, QStringLiteral("editable grid update failed: %1").arg(error));
    vsdb::QueryResult updated = session.execute(
        QStringLiteral("SELECT name FROM %1 WHERE id = 1")
            .arg(session.qualifiedName(schema, QStringLiteral("items"))), 100, &error);
    if (!error.isEmpty() || updated.rows.constFirst().constFirst().toString() != QStringLiteral("updated"))
        return fail(10, QStringLiteral("updated value not persisted: %1").arg(error));

    vsdb::QueryResult update = session.execute(
        QStringLiteral("UPDATE %1 SET active = false WHERE id = 2")
            .arg(session.qualifiedName(schema, QStringLiteral("items"))), 100, &error);
    if (!error.isEmpty() || update.affectedRows != 1)
        return fail(11, QStringLiteral("update failed: %1").arg(error));
    vsdb::QueryResult deletion = session.execute(
        QStringLiteral("DELETE FROM %1 WHERE id = 2")
            .arg(session.qualifiedName(schema, QStringLiteral("items"))), 100, &error);
    if (!error.isEmpty() || deletion.affectedRows != 1)
        return fail(12, QStringLiteral("delete failed: %1").arg(error));

    const QString role = QStringLiteral("vsdb_switch_user");
    exec(session, QStringLiteral("DROP ROLE IF EXISTS %1").arg(session.quoteIdentifier(role)), &error);
    if (!exec(session, QStringLiteral("CREATE ROLE %1 LOGIN").arg(session.quoteIdentifier(role)), &error)
        || !session.switchUser(role, &error))
        return fail(13, QStringLiteral("user switch failed: %1").arg(error));
    vsdb::QueryResult switchedUser = session.execute(QStringLiteral("SELECT current_user"), 1, &error);
    if (!error.isEmpty() || switchedUser.rows.constFirst().constFirst().toString() != role)
        return fail(14, QStringLiteral("current user mismatch: %1").arg(error));
    if (!session.switchUser(config.user, &error))
        return fail(15, QStringLiteral("user reset failed: %1").arg(error));
    if (!exec(session, QStringLiteral("DROP ROLE %1").arg(session.quoteIdentifier(role)), &error))
        return fail(16, QStringLiteral("role cleanup failed: %1").arg(error));

    const QString database = QStringLiteral("vsdb_switch_database");
    exec(session, QStringLiteral("DROP DATABASE IF EXISTS %1").arg(session.quoteIdentifier(database)), &error);
    if (!exec(session, QStringLiteral("CREATE DATABASE %1").arg(session.quoteIdentifier(database)), &error)
        || !session.switchDatabase(database, &error))
        return fail(17, QStringLiteral("database switch failed: %1").arg(error));
    vsdb::QueryResult switchedDatabase = session.execute(QStringLiteral("SELECT current_database()"), 1, &error);
    if (!error.isEmpty() || switchedDatabase.rows.constFirst().constFirst().toString() != database)
        return fail(18, QStringLiteral("current database mismatch: %1").arg(error));
    if (!session.switchDatabase(config.database, &error)
        || !exec(session, QStringLiteral("DROP DATABASE %1").arg(session.quoteIdentifier(database)), &error))
        return fail(19, QStringLiteral("database cleanup failed: %1").arg(error));

    if (!exec(session, QStringLiteral("DROP SCHEMA %1 CASCADE")
                           .arg(session.quoteIdentifier(schema)), &error))
        return fail(20, QStringLiteral("schema cleanup failed: %1").arg(error));
    return 0;
}

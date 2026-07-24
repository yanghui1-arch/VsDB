#pragma once

#include "database/DatabaseTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace vsdb {

class PostgresSession final
{
public:
    PostgresSession();
    ~PostgresSession();

    PostgresSession(const PostgresSession &) = delete;
    PostgresSession &operator=(const PostgresSession &) = delete;

    bool connectToServer(const PostgresConnectionConfig &config, QString *error);
    bool reconnect(QString *error);
    void disconnect();
    bool disconnectDatabase(const QString &database);
    bool isConnected() const;
    bool isDatabaseConnected(const QString &database) const;
    QStringList connectedDatabases() const;

    const PostgresConnectionConfig &config() const;
    QString currentUser() const;
    QString serverVersion() const;

    bool switchUser(const QString &user, QString *error);
    bool switchDatabase(const QString &database, QString *error);

    QStringList users(QString *error) const;
    QStringList databases(QString *error) const;
    QStringList schemas(QString *error) const;
    QVector<DatabaseRelation> relations(const QString &schema, QString *error) const;
    DatabaseTable describeTable(const QString &schema, const QString &table,
                                QString *error) const;
    QString buildRelationPreview(const DatabaseTable &table,
                                 int rowLimit = 100) const;

    QueryResult execute(const QString &sql, int rowLimit, QString *error) const;
    bool cancelBackend(qint64 backendPid, QString *error) const;
    bool applyChanges(const QString &schema, const QString &table,
                      const QVector<QueryColumn> &columns,
                      const QVector<QVariantList> &originalRows,
                      const QStringList &primaryKeys,
                      const QVector<CellChange> &changes,
                      QString *error) const;

    QString quoteIdentifier(const QString &identifier) const;
    QString qualifiedName(const QString &schema, const QString &relation) const;

private:
    bool activateDatabase(const QString &database, const QString &sessionUser,
                          QString *error);
    bool openDatabaseConnection(const PostgresConnectionConfig &config,
                                const QString &database,
                                const QString &sessionUser,
                                QString *connectionName, QString *error);
    bool reopenActiveDatabase(QString *error);
    void closeAndRemoveConnection(const QString &connectionName);
    bool hasSameServerIdentity(const PostgresConnectionConfig &config) const;
    bool setSessionUser(const QString &user, QString *error);
    QString databaseError() const;

    QString connectionPrefix_;
    QString activeConnectionName_;
    QHash<QString, QString> databaseConnections_;
    QHash<QString, QString> databaseUsers_;
    PostgresConnectionConfig config_;
    QString currentUser_;
    bool configured_ = false;
};

} // namespace vsdb

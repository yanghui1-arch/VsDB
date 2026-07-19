#pragma once

#include "database/DatabaseTypes.h"

#include <QString>

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
    bool isConnected() const;

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
    bool openDatabase(const QString &database, QString *error);
    bool setSessionUser(const QString &user, QString *error);
    QString databaseError() const;

    QString connectionName_;
    PostgresConnectionConfig config_;
    QString currentUser_;
    bool configured_ = false;
};

} // namespace vsdb

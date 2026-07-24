#pragma once

#include "database/DatabaseTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

class QSettings;

namespace vsdb {

struct ConnectionSnapshot
{
    QStringList users;
    QStringList databases;
    QStringList schemas;
    QStringList publicTables;
    QStringList publicViews;
};

struct SavedConnection
{
    QString id;
    PostgresConnectionConfig config;
    ConnectionSnapshot snapshot;
    bool hasStoredPassword = false;
};

class CredentialStore
{
public:
    virtual ~CredentialStore() = default;

    virtual bool write(const QString &key, const QString &secret, QString *error) = 0;
    virtual bool read(const QString &key, QString *secret, QString *error) const = 0;
    virtual bool remove(const QString &key, QString *error) = 0;
};

std::unique_ptr<CredentialStore> createPlatformCredentialStore();

class ConnectionStore final
{
public:
    static QVector<SavedConnection> load(QSettings &settings,
                                         const CredentialStore &credentials,
                                         QStringList *warnings = nullptr);
    static bool upsert(QSettings &settings, CredentialStore &credentials,
                       SavedConnection &connection, QString *error = nullptr);
    static bool saveSnapshot(QSettings &settings, const SavedConnection &connection,
                             QString *error = nullptr);
    static bool remove(QSettings &settings, CredentialStore &credentials,
                       const QString &connectionId, QString *error = nullptr);

private:
    static QString credentialKey(const QString &connectionId);
};

} // namespace vsdb

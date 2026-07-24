#pragma once

#include "database/ConnectionStore.h"
#include "database/redis/RedisSession.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QSettings;

namespace vsdb {

struct SavedRedisConnection
{
    QString id;
    // QSettings stores only non-sensitive fields; password comes from CredentialStore.
    RedisConnectionConfig config;
    // Only database indexes and aggregate counts are cached; key names and
    // values are intentionally never persisted.
    QVector<RedisDatabaseInfo> databaseSnapshot;
    bool hasStoredPassword = false;
};

class RedisConnectionStore final
{
public:
    static QVector<SavedRedisConnection> load(
        QSettings &settings, const CredentialStore &credentials,
        QStringList *warnings = nullptr);
    static bool upsert(QSettings &settings, CredentialStore &credentials,
                       SavedRedisConnection &connection,
                       QString *error = nullptr);
    static bool saveSnapshot(
        QSettings &settings, const SavedRedisConnection &connection,
        QString *error = nullptr);
    static bool remove(QSettings &settings, CredentialStore &credentials,
                       const QString &connectionId, QString *error = nullptr);

private:
    static QString credentialKey(const QString &connectionId);
};

} // namespace vsdb

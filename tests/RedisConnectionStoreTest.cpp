#include "database/redis/RedisConnectionStore.h"

#include <QCoreApplication>
#include <QHash>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

namespace {

class MemoryCredentialStore final : public vsdb::CredentialStore
{
public:
    bool write(const QString &key, const QString &secret, QString *) override
    {
        secrets.insert(key, secret);
        return true;
    }

    bool read(const QString &key, QString *secret, QString *) const override
    {
        const auto found = secrets.constFind(key);
        if (found == secrets.constEnd())
            return false;
        if (secret)
            *secret = found.value();
        return true;
    }

    bool remove(const QString &key, QString *) override
    {
        secrets.remove(key);
        return true;
    }

    QHash<QString, QString> secrets;
};

int fail(int code, const char *message)
{
    std::cerr << message << '\n';
    return code;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
        return fail(1, "temporary directory creation failed");

    QSettings settings(
        temporaryDirectory.filePath(QStringLiteral("redis-connections.ini")),
        QSettings::IniFormat);
    MemoryCredentialStore credentials;
    vsdb::SavedRedisConnection connection;
    connection.config.host = QStringLiteral("redis.internal.example");
    connection.config.port = 6380;
    connection.config.username = QStringLiteral("nexus");
    connection.config.password = QStringLiteral("redis secret");
    connection.config.database = 4;
    connection.config.tls = true;
    connection.config.connectTimeoutSeconds = 12;

    QString error;
    if (!vsdb::RedisConnectionStore::upsert(
            settings, credentials, connection, &error))
        return fail(2, "saving Redis connection failed");
    if (connection.id.isEmpty() || !connection.hasStoredPassword)
        return fail(3, "saved Redis identity or secret state is invalid");
    connection.databaseSnapshot = {
        {0, 9, 2},
        {1, 3, 0},
        {2, 1, 0}
    };
    if (!vsdb::RedisConnectionStore::saveSnapshot(
            settings, connection, &error)) {
        return fail(12, "saving Redis database snapshot failed");
    }

    for (const QString &key : settings.allKeys()) {
        if (settings.value(key).toString() == connection.config.password)
            return fail(4, "Redis password leaked into QSettings");
    }

    QStringList warnings;
    const QVector<vsdb::SavedRedisConnection> loaded =
        vsdb::RedisConnectionStore::load(settings, credentials, &warnings);
    if (!warnings.isEmpty() || loaded.size() != 1)
        return fail(5, "saved Redis connection did not load cleanly");
    const vsdb::SavedRedisConnection &restored = loaded.constFirst();
    if (restored.id != connection.id
        || restored.config.host != connection.config.host
        || restored.config.port != connection.config.port
        || restored.config.username != connection.config.username
        || restored.config.password != connection.config.password
        || restored.config.database != connection.config.database
        || restored.config.tls != connection.config.tls
        || restored.config.connectTimeoutSeconds
            != connection.config.connectTimeoutSeconds
        || restored.databaseSnapshot.size() != 3
        || restored.databaseSnapshot.at(0).index != 0
        || restored.databaseSnapshot.at(0).keys != 9
        || restored.databaseSnapshot.at(0).expires != 2
        || restored.databaseSnapshot.at(2).index != 2
        || restored.databaseSnapshot.at(2).keys != 1
        || !restored.hasStoredPassword) {
        return fail(6, "saved Redis fields were not restored");
    }

    connection.config.password = QStringLiteral("replacement");
    connection.config.database = 7;
    if (!vsdb::RedisConnectionStore::upsert(
            settings, credentials, connection, &error))
        return fail(7, "updating Redis connection failed");
    if (settings.value(
            QStringLiteral("redisConnections/order")).toStringList().size() != 1)
        return fail(8, "updating duplicated Redis connection");
    const QVector<vsdb::SavedRedisConnection> updated =
        vsdb::RedisConnectionStore::load(settings, credentials);
    if (updated.size() != 1
        || updated.constFirst().databaseSnapshot.size() != 3
        || updated.constFirst().databaseSnapshot.at(1).index != 1
        || updated.constFirst().databaseSnapshot.at(1).keys != 3) {
        return fail(13, "updating Redis profile lost its database snapshot");
    }

    MemoryCredentialStore missingCredentials;
    warnings.clear();
    const QVector<vsdb::SavedRedisConnection> missingPassword =
        vsdb::RedisConnectionStore::load(
            settings, missingCredentials, &warnings);
    if (missingPassword.size() != 1
        || missingPassword.constFirst().hasStoredPassword
        || warnings.size() != 1) {
        return fail(9, "missing Redis password should preserve visible profile");
    }

    if (!vsdb::RedisConnectionStore::remove(
            settings, credentials, connection.id, &error))
        return fail(10, "removing Redis connection failed");
    if (!vsdb::RedisConnectionStore::load(settings, credentials).isEmpty()
        || !credentials.secrets.isEmpty())
        return fail(11, "Redis metadata or secret remained after removal");

    return 0;
}

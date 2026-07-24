#include "database/redis/RedisConnectionStore.h"

#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace vsdb {

namespace {

void assignError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString profileGroup(const QString &connectionId)
{
    return QStringLiteral("redisConnections/profiles/%1").arg(connectionId);
}

QString encodeDatabase(const RedisDatabaseInfo &database)
{
    return QStringLiteral("%1:%2:%3")
        .arg(database.index)
        .arg(database.keys)
        .arg(database.expires);
}

bool decodeDatabase(const QString &encoded, RedisDatabaseInfo *database)
{
    const QStringList fields = encoded.split(QLatin1Char(':'));
    if (fields.size() != 3)
        return false;
    bool indexValid = false;
    bool keysValid = false;
    bool expiresValid = false;
    const qlonglong index = fields.at(0).toLongLong(&indexValid);
    const qlonglong keys = fields.at(1).toLongLong(&keysValid);
    const qlonglong expires = fields.at(2).toLongLong(&expiresValid);
    if (!indexValid || !keysValid || !expiresValid
        || index < 0 || index > std::numeric_limits<int>::max()
        || keys < 0 || expires < 0) {
        return false;
    }
    if (database)
        *database = {static_cast<int>(index), keys, expires};
    return true;
}

} // namespace

QVector<SavedRedisConnection> RedisConnectionStore::load(
    QSettings &settings, const CredentialStore &credentials,
    QStringList *warnings)
{
    QVector<SavedRedisConnection> result;
    const QStringList order =
        settings.value(QStringLiteral("redisConnections/order")).toStringList();
    result.reserve(order.size());

    for (const QString &id : order) {
        if (id.isEmpty())
            continue;
        settings.beginGroup(profileGroup(id));
        SavedRedisConnection connection;
        connection.id = id;
        connection.config.host = settings.value(QStringLiteral("host")).toString();
        connection.config.port =
            settings.value(QStringLiteral("port"), 6379).toInt();
        connection.config.username =
            settings.value(QStringLiteral("username")).toString();
        connection.config.database =
            settings.value(QStringLiteral("database"), 0).toInt();
        connection.config.tls =
            settings.value(QStringLiteral("tls"), false).toBool();
        connection.config.connectTimeoutSeconds =
            settings.value(QStringLiteral("connectTimeout"), 10).toInt();
        const QStringList databaseSnapshot =
            settings.value(
                QStringLiteral("snapshot/databases")).toStringList();
        connection.databaseSnapshot.reserve(databaseSnapshot.size());
        for (const QString &encoded : databaseSnapshot) {
            RedisDatabaseInfo database;
            if (decodeDatabase(encoded, &database))
                connection.databaseSnapshot.append(database);
        }
        std::sort(
            connection.databaseSnapshot.begin(),
            connection.databaseSnapshot.end(),
            [](const RedisDatabaseInfo &left,
               const RedisDatabaseInfo &right) {
                return left.index < right.index;
            });
        settings.endGroup();

        if (connection.config.host.isEmpty() || connection.config.port < 1
            || connection.config.port > 65535
            || connection.config.database < 0
            || connection.config.connectTimeoutSeconds < 1
            || connection.config.connectTimeoutSeconds > 60) {
            if (warnings) {
                warnings->append(
                    QStringLiteral("已跳过损坏的 Redis 连接配置：%1").arg(id));
            }
            continue;
        }

        QString credentialError;
        connection.hasStoredPassword = credentials.read(
            credentialKey(id), &connection.config.password, &credentialError);
        if (!connection.hasStoredPassword && warnings) {
            const QString reason = credentialError.isEmpty()
                ? QStringLiteral("找不到已保存的密码")
                : credentialError;
            warnings->append(QStringLiteral("%1：%2")
                                 .arg(connection.config.displayName(), reason));
        }
        result.append(std::move(connection));
    }
    return result;
}

bool RedisConnectionStore::upsert(
    QSettings &settings, CredentialStore &credentials,
    SavedRedisConnection &connection, QString *error)
{
    if (connection.config.host.trimmed().isEmpty()
        || connection.config.port < 1 || connection.config.port > 65535
        || connection.config.database < 0
        || connection.config.connectTimeoutSeconds < 1
        || connection.config.connectTimeoutSeconds > 60) {
        assignError(error, QStringLiteral("Redis 连接地址、端口、数据库或超时设置无效。"));
        return false;
    }

    const bool isNew = connection.id.isEmpty();
    if (isNew)
        connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (!credentials.write(credentialKey(connection.id),
                           connection.config.password, error)) {
        if (isNew)
            connection.id.clear();
        return false;
    }

    settings.beginGroup(profileGroup(connection.id));
    settings.setValue(QStringLiteral("host"), connection.config.host.trimmed());
    settings.setValue(QStringLiteral("port"), connection.config.port);
    settings.setValue(QStringLiteral("username"), connection.config.username);
    settings.setValue(QStringLiteral("database"), connection.config.database);
    settings.setValue(QStringLiteral("tls"), connection.config.tls);
    settings.setValue(QStringLiteral("connectTimeout"),
                      connection.config.connectTimeoutSeconds);
    settings.endGroup();

    QStringList order =
        settings.value(QStringLiteral("redisConnections/order")).toStringList();
    if (!order.contains(connection.id)) {
        order.append(connection.id);
        settings.setValue(QStringLiteral("redisConnections/order"), order);
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        assignError(error, QStringLiteral("Redis 连接配置无法写入本地设置。"));
        if (isNew) {
            QString ignoredError;
            credentials.remove(credentialKey(connection.id), &ignoredError);
            connection.id.clear();
        }
        return false;
    }

    connection.config.host = connection.config.host.trimmed();
    connection.hasStoredPassword = true;
    return true;
}

bool RedisConnectionStore::saveSnapshot(
    QSettings &settings, const SavedRedisConnection &connection,
    QString *error)
{
    if (connection.id.isEmpty()) {
        assignError(error, QStringLiteral("Redis 连接尚未保存。"));
        return false;
    }
    QStringList databases;
    databases.reserve(connection.databaseSnapshot.size());
    for (const RedisDatabaseInfo &database :
         connection.databaseSnapshot) {
        if (database.index < 0 || database.keys < 0
            || database.expires < 0) {
            continue;
        }
        databases.append(encodeDatabase(database));
    }
    settings.beginGroup(profileGroup(connection.id));
    settings.setValue(QStringLiteral("snapshot/databases"), databases);
    settings.endGroup();
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    assignError(
        error, QStringLiteral("Redis 数据库快照无法写入本地设置。"));
    return false;
}

bool RedisConnectionStore::remove(
    QSettings &settings, CredentialStore &credentials,
    const QString &connectionId, QString *error)
{
    if (connectionId.isEmpty()) {
        assignError(error, QStringLiteral("没有选择要删除的 Redis 连接。"));
        return false;
    }
    if (!credentials.remove(credentialKey(connectionId), error))
        return false;

    settings.remove(profileGroup(connectionId));
    QStringList order =
        settings.value(QStringLiteral("redisConnections/order")).toStringList();
    order.removeAll(connectionId);
    settings.setValue(QStringLiteral("redisConnections/order"), order);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        assignError(error, QStringLiteral("Redis 连接配置无法从本地设置中删除。"));
        return false;
    }
    return true;
}

QString RedisConnectionStore::credentialKey(const QString &connectionId)
{
    return QStringLiteral("VsDB/Redis/%1").arg(connectionId);
}

} // namespace vsdb

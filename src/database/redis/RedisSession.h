#pragma once

#include "database/redis/RedisProtocol.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <memory>

class QAbstractSocket;

namespace vsdb {

inline constexpr int DefaultRedisPreviewItems = 200;
inline constexpr qint64 DefaultRedisValuePreviewBytes = 1024 * 1024;

struct RedisConnectionConfig
{
    QString host = QStringLiteral("localhost");
    int port = 6379;
    QString username;
    QString password;
    int database = 0;
    bool tls = false;
    int connectTimeoutSeconds = 10;

    QString displayName() const;
};

struct RedisDatabaseInfo
{
    int index = 0;
    qlonglong keys = 0;
    qlonglong expires = 0;
};

struct RedisScanPage
{
    QByteArray nextCursor = QByteArrayLiteral("0");
    QVector<QByteArray> keys;
};

struct RedisEntry
{
    QByteArray identity;
    QByteArray label;
    QByteArray value;
};

struct RedisKeyDetails
{
    QByteArray key;
    QByteArray type;
    qint64 ttlMilliseconds = -1;
    qlonglong size = 0;
    QByteArray stringValue;
    QVector<RedisEntry> entries;
    bool truncated = false;
};

class RedisSession final
{
public:
    RedisSession();
    ~RedisSession();

    RedisSession(const RedisSession &) = delete;
    RedisSession &operator=(const RedisSession &) = delete;

    bool connectToServer(const RedisConnectionConfig &config, QString *error);
    bool reconnect(QString *error);
    void disconnect();
    bool isConnected() const;
    const RedisConnectionConfig &config() const;

    bool selectDatabase(int database, QString *error);
    QVector<RedisDatabaseInfo> databases(QString *error);
    RedisScanPage scanKeys(const QByteArray &cursor, const QByteArray &pattern,
                           int count, QString *error);
    RedisKeyDetails inspectKey(
        const QByteArray &key, int itemLimit = DefaultRedisPreviewItems,
        qint64 valuePreviewBytes = DefaultRedisValuePreviewBytes,
        QString *error = nullptr);

    bool createString(const QByteArray &key, const QByteArray &value,
                      qint64 ttlMilliseconds, QString *error);
    bool createCollection(const QByteArray &type, const QByteArray &key,
                          const QByteArray &identity,
                          const QByteArray &value,
                          qint64 ttlMilliseconds, QString *error);
    bool updateString(const QByteArray &key, const QByteArray &value,
                      QString *error);
    bool renameKey(const QByteArray &key, const QByteArray &newKey,
                   QString *error);
    bool deleteKey(const QByteArray &key, QString *error);
    bool addEntry(const QByteArray &type, const QByteArray &key,
                  const QByteArray &identity, const QByteArray &value,
                  QString *error);
    bool updateEntry(const QByteArray &type, const QByteArray &key,
                     const QByteArray &identity, const QByteArray &value,
                     QString *error);
    bool deleteEntry(const QByteArray &type, const QByteArray &key,
                     const QByteArray &identity, QString *error);

private:
    bool sendCommand(const QVector<QByteArray> &arguments,
                     RedisReply *reply, QString *error);
    bool expectInteger(const QVector<QByteArray> &arguments,
                       qint64 *value, QString *error);
    bool validateKey(const QByteArray &key, QString *error) const;
    void resetSocket();

    std::unique_ptr<QAbstractSocket> socket_;
    RedisConnectionConfig config_;
    bool configured_ = false;
};

} // namespace vsdb

#include "database/redis/RedisSession.h"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>

#include <algorithm>

namespace vsdb {

namespace {

constexpr qint64 MaximumReplyBufferBytes = 16LL * 1024 * 1024;
constexpr qsizetype MaximumCommandArguments = 1024;
constexpr qint64 MaximumCommandBytes = 8LL * 1024 * 1024;

void assignError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QByteArray replyBytes(const RedisReply &reply)
{
    if (reply.type == RedisReply::Type::BulkString
        || reply.type == RedisReply::Type::SimpleString) {
        return reply.bytes;
    }
    return {};
}

bool replyIsStatus(const RedisReply &reply, const QByteArray &status)
{
    return reply.type == RedisReply::Type::SimpleString
        && reply.bytes.compare(status, Qt::CaseInsensitive) == 0;
}

bool parseScanReply(const RedisReply &reply, QByteArray *cursor,
                    QVector<RedisReply> *items, QString *error)
{
    if (reply.type != RedisReply::Type::Array || reply.elements.size() != 2
        || reply.elements.at(1).type != RedisReply::Type::Array) {
        assignError(error, QStringLiteral("Redis 返回了无效的 SCAN 响应。"));
        return false;
    }
    *cursor = replyBytes(reply.elements.constFirst());
    if (cursor->isEmpty())
        *cursor = QByteArrayLiteral("0");
    *items = reply.elements.at(1).elements;
    return true;
}

qint64 integerReply(const RedisReply &reply, bool *ok)
{
    if (reply.type == RedisReply::Type::Integer) {
        *ok = true;
        return reply.integer;
    }
    bool parsed = false;
    const qint64 value = replyBytes(reply).toLongLong(&parsed);
    *ok = parsed;
    return value;
}

} // namespace

QString RedisConnectionConfig::displayName() const
{
    const QString identity = username.isEmpty()
        ? host
        : QStringLiteral("%1@%2").arg(username, host);
    return QStringLiteral("%1:%2/db%3").arg(identity).arg(port).arg(database);
}

RedisSession::RedisSession() = default;

RedisSession::~RedisSession()
{
    disconnect();
}

bool RedisSession::connectToServer(const RedisConnectionConfig &config,
                                   QString *error)
{
    if (config.host.trimmed().isEmpty() || config.port < 1
        || config.port > 65535 || config.database < 0
        || config.connectTimeoutSeconds < 1
        || config.connectTimeoutSeconds > 60) {
        assignError(error, QStringLiteral("Redis 连接地址、端口、数据库或超时设置无效。"));
        return false;
    }

    disconnect();
    config_ = config;
    config_.host = config_.host.trimmed();
    const int timeout = config_.connectTimeoutSeconds * 1000;

    if (config_.tls) {
        auto sslSocket = std::make_unique<QSslSocket>();
        sslSocket->setPeerVerifyMode(QSslSocket::VerifyPeer);
        sslSocket->connectToHostEncrypted(config_.host,
                                          static_cast<quint16>(config_.port));
        if (!sslSocket->waitForEncrypted(timeout)) {
            assignError(error, QStringLiteral("Redis TLS 连接失败：%1")
                                   .arg(sslSocket->errorString()));
            return false;
        }
        socket_ = std::move(sslSocket);
    } else {
        auto tcpSocket = std::make_unique<QTcpSocket>();
        tcpSocket->connectToHost(config_.host, static_cast<quint16>(config_.port));
        if (!tcpSocket->waitForConnected(timeout)) {
            assignError(error, QStringLiteral("Redis 连接失败：%1")
                                   .arg(tcpSocket->errorString()));
            return false;
        }
        socket_ = std::move(tcpSocket);
    }

    RedisReply reply;
    if (!config_.password.isEmpty() || !config_.username.isEmpty()) {
        QVector<QByteArray> auth{QByteArrayLiteral("AUTH")};
        if (!config_.username.isEmpty())
            auth.append(config_.username.toUtf8());
        auth.append(config_.password.toUtf8());
        if (!sendCommand(auth, &reply, error)
            || !replyIsStatus(reply, QByteArrayLiteral("OK"))) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Redis 身份验证失败。");
            resetSocket();
            return false;
        }
    }

    if (!selectDatabase(config_.database, error)) {
        resetSocket();
        return false;
    }
    if (!sendCommand({QByteArrayLiteral("PING")}, &reply, error)
        || !replyIsStatus(reply, QByteArrayLiteral("PONG"))) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis PING 校验失败。");
        resetSocket();
        return false;
    }

    configured_ = true;
    return true;
}

bool RedisSession::reconnect(QString *error)
{
    if (!configured_) {
        assignError(error, QStringLiteral("尚未配置 Redis 连接。"));
        return false;
    }
    return connectToServer(config_, error);
}

void RedisSession::disconnect()
{
    resetSocket();
}

bool RedisSession::isConnected() const
{
    return socket_
        && socket_->state() == QAbstractSocket::ConnectedState;
}

const RedisConnectionConfig &RedisSession::config() const
{
    return config_;
}

bool RedisSession::selectDatabase(int database, QString *error)
{
    if (database < 0) {
        assignError(error, QStringLiteral("Redis 数据库编号不能为负数。"));
        return false;
    }
    RedisReply reply;
    if (!sendCommand({QByteArrayLiteral("SELECT"),
                      QByteArray::number(database)}, &reply, error)) {
        return false;
    }
    if (!replyIsStatus(reply, QByteArrayLiteral("OK"))) {
        assignError(error, QStringLiteral("Redis 拒绝切换数据库。"));
        return false;
    }
    config_.database = database;
    return true;
}

QVector<RedisDatabaseInfo> RedisSession::databases(QString *error)
{
    QVector<RedisDatabaseInfo> result;
    RedisReply reply;
    QString infoError;
    if (sendCommand({QByteArrayLiteral("INFO"), QByteArrayLiteral("keyspace")},
                    &reply, &infoError)) {
        const QList<QByteArray> lines = replyBytes(reply).split('\n');
        const QRegularExpression expression(
            QStringLiteral("^db(\\d+):keys=(\\d+),expires=(\\d+)"));
        for (QByteArray line : lines) {
            line = line.trimmed();
            const QRegularExpressionMatch match =
                expression.match(QString::fromLatin1(line));
            if (!match.hasMatch())
                continue;
            RedisDatabaseInfo database;
            database.index = match.captured(1).toInt();
            database.keys = match.captured(2).toLongLong();
            database.expires = match.captured(3).toLongLong();
            result.append(database);
        }
    }

    const auto current = std::find_if(
        result.cbegin(), result.cend(), [this](const RedisDatabaseInfo &database) {
            return database.index == config_.database;
        });
    if (current == result.cend()) {
        qint64 keyCount = 0;
        QString countError;
        if (!expectInteger({QByteArrayLiteral("DBSIZE")}, &keyCount, &countError))
            keyCount = 0;
        result.append({config_.database, keyCount, 0});
    }
    std::sort(result.begin(), result.end(),
              [](const RedisDatabaseInfo &left, const RedisDatabaseInfo &right) {
                  return left.index < right.index;
              });
    if (error)
        error->clear();
    return result;
}

RedisScanPage RedisSession::scanKeys(const QByteArray &cursor,
                                     const QByteArray &pattern, int count,
                                     QString *error)
{
    RedisScanPage page;
    if (count < 10 || count > 1000 || pattern.size() > 1024) {
        assignError(error, QStringLiteral("Redis SCAN 分页参数超过安全限制。"));
        return page;
    }
    bool cursorOk = false;
    cursor.toULongLong(&cursorOk);
    if (!cursorOk) {
        assignError(error, QStringLiteral("Redis SCAN 游标无效。"));
        return page;
    }

    QVector<QByteArray> command{QByteArrayLiteral("SCAN"), cursor,
                                QByteArrayLiteral("COUNT"),
                                QByteArray::number(count)};
    if (!pattern.isEmpty()) {
        command.append(QByteArrayLiteral("MATCH"));
        command.append(pattern);
    }
    RedisReply reply;
    if (!sendCommand(command, &reply, error))
        return page;

    QVector<RedisReply> items;
    if (!parseScanReply(reply, &page.nextCursor, &items, error))
        return page;
    page.keys.reserve(items.size());
    for (const RedisReply &item : items) {
        page.keys.append(replyBytes(item));
    }
    return page;
}

RedisKeyDetails RedisSession::inspectKey(const QByteArray &key, int itemLimit,
                                         qint64 valuePreviewBytes,
                                         QString *error)
{
    RedisKeyDetails details;
    details.key = key;
    if (!validateKey(key, error) || itemLimit < 1 || itemLimit > 1000
        || valuePreviewBytes < 1
        || valuePreviewBytes > DefaultRedisValuePreviewBytes) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis 预览参数超过安全限制。");
        return details;
    }

    RedisReply typeReply;
    if (!sendCommand({QByteArrayLiteral("TYPE"), key}, &typeReply, error))
        return details;
    details.type = replyBytes(typeReply).toLower();
    if (details.type == QByteArrayLiteral("none")) {
        assignError(error, QStringLiteral("Redis 键已不存在。"));
        return details;
    }

    qint64 ttl = -1;
    if (!expectInteger({QByteArrayLiteral("PTTL"), key}, &ttl, error))
        return {};
    details.ttlMilliseconds = ttl;

    RedisReply reply;
    if (details.type == QByteArrayLiteral("string")) {
        qint64 length = 0;
        if (!expectInteger({QByteArrayLiteral("STRLEN"), key}, &length, error))
            return {};
        details.size = length;
        if (!sendCommand({QByteArrayLiteral("GETRANGE"), key,
                          QByteArrayLiteral("0"),
                          QByteArray::number(valuePreviewBytes - 1)},
                         &reply, error)) {
            return {};
        }
        details.stringValue = replyBytes(reply);
        details.truncated = length > details.stringValue.size();
        return details;
    }

    QByteArray sizeCommand;
    if (details.type == QByteArrayLiteral("hash"))
        sizeCommand = QByteArrayLiteral("HLEN");
    else if (details.type == QByteArrayLiteral("list"))
        sizeCommand = QByteArrayLiteral("LLEN");
    else if (details.type == QByteArrayLiteral("set"))
        sizeCommand = QByteArrayLiteral("SCARD");
    else if (details.type == QByteArrayLiteral("zset"))
        sizeCommand = QByteArrayLiteral("ZCARD");
    else if (details.type == QByteArrayLiteral("stream"))
        sizeCommand = QByteArrayLiteral("XLEN");
    else {
        assignError(error, QStringLiteral("VsDB 暂不支持预览 Redis 类型：%1")
                               .arg(QString::fromLatin1(details.type)));
        return details;
    }
    if (!expectInteger({sizeCommand, key}, &details.size, error))
        return {};

    if (details.type == QByteArrayLiteral("list")) {
        if (!sendCommand({QByteArrayLiteral("LRANGE"), key,
                          QByteArrayLiteral("0"),
                          QByteArray::number(itemLimit - 1)}, &reply, error)) {
            return {};
        }
        if (reply.type != RedisReply::Type::Array) {
            assignError(error, QStringLiteral("Redis 返回了无效的列表响应。"));
            return {};
        }
        details.entries.reserve(reply.elements.size());
        for (qsizetype index = 0; index < reply.elements.size(); ++index) {
            const QByteArray indexBytes = QByteArray::number(index);
            details.entries.append(
                {indexBytes, indexBytes, replyBytes(reply.elements.at(index))});
        }
        details.truncated = details.size > details.entries.size();
        return details;
    }

    if (details.type == QByteArrayLiteral("stream")) {
        if (!sendCommand({QByteArrayLiteral("XRANGE"), key,
                          QByteArrayLiteral("-"), QByteArrayLiteral("+"),
                          QByteArrayLiteral("COUNT"),
                          QByteArray::number(itemLimit)}, &reply, error)) {
            return {};
        }
        if (reply.type != RedisReply::Type::Array) {
            assignError(error, QStringLiteral("Redis 返回了无效的 Stream 响应。"));
            return {};
        }
        for (const RedisReply &message : reply.elements) {
            if (message.type != RedisReply::Type::Array
                || message.elements.size() != 2
                || message.elements.at(1).type != RedisReply::Type::Array) {
                continue;
            }
            const QByteArray id = replyBytes(message.elements.constFirst());
            const QVector<RedisReply> fields = message.elements.at(1).elements;
            for (qsizetype index = 0; index + 1 < fields.size(); index += 2) {
                details.entries.append(
                    {id, id + QByteArrayLiteral(" / ")
                             + replyBytes(fields.at(index)),
                     replyBytes(fields.at(index + 1))});
            }
        }
        details.truncated = details.size
            > (reply.type == RedisReply::Type::Array
                   ? reply.elements.size() : 0);
        return details;
    }

    const QByteArray scanCommand =
        details.type == QByteArrayLiteral("hash")
        ? QByteArrayLiteral("HSCAN")
        : details.type == QByteArrayLiteral("set")
            ? QByteArrayLiteral("SSCAN")
            : QByteArrayLiteral("ZSCAN");
    if (!sendCommand({scanCommand, key, QByteArrayLiteral("0"),
                      QByteArrayLiteral("COUNT"), QByteArray::number(itemLimit)},
                     &reply, error)) {
        return {};
    }
    QByteArray cursor;
    QVector<RedisReply> items;
    if (!parseScanReply(reply, &cursor, &items, error))
        return {};

    if (details.type == QByteArrayLiteral("set")) {
        for (const RedisReply &item : items) {
            if (details.entries.size() >= itemLimit)
                break;
            const QByteArray member = replyBytes(item);
            details.entries.append({member, member, {}});
        }
    } else {
        for (qsizetype index = 0;
             index + 1 < items.size()
             && details.entries.size() < itemLimit;
             index += 2) {
            const QByteArray identity = replyBytes(items.at(index));
            details.entries.append(
                {identity, identity, replyBytes(items.at(index + 1))});
        }
    }
    details.truncated = cursor != QByteArrayLiteral("0")
        || details.size > details.entries.size();
    return details;
}

bool RedisSession::createString(const QByteArray &key, const QByteArray &value,
                                qint64 ttlMilliseconds, QString *error)
{
    if (!validateKey(key, error) || ttlMilliseconds < 0) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis TTL 不能为负数。");
        return false;
    }
    QVector<QByteArray> command{QByteArrayLiteral("SET"), key, value,
                                QByteArrayLiteral("NX")};
    if (ttlMilliseconds > 0) {
        command.append(QByteArrayLiteral("PX"));
        command.append(QByteArray::number(ttlMilliseconds));
    }
    RedisReply reply;
    if (!sendCommand(command, &reply, error))
        return false;
    if (reply.type == RedisReply::Type::Null) {
        assignError(error, QStringLiteral("同名 Redis 键已经存在；未覆盖现有数据。"));
        return false;
    }
    return replyIsStatus(reply, QByteArrayLiteral("OK"));
}

bool RedisSession::createCollection(
    const QByteArray &type, const QByteArray &key,
    const QByteArray &identity, const QByteArray &value,
    qint64 ttlMilliseconds, QString *error)
{
    static const QByteArray script = QByteArrayLiteral(
        "if redis.call('exists',KEYS[1])~=0 then return 0 end "
        "local t=ARGV[1] "
        "if t=='hash' then redis.call('hset',KEYS[1],ARGV[2],ARGV[3]) "
        "elseif t=='list' then redis.call('rpush',KEYS[1],ARGV[2]) "
        "elseif t=='set' then redis.call('sadd',KEYS[1],ARGV[2]) "
        "elseif t=='zset' then redis.call('zadd',KEYS[1],ARGV[3],ARGV[2]) "
        "elseif t=='stream' then redis.call('xadd',KEYS[1],'*',ARGV[2],ARGV[3]) "
        "else return -1 end "
        "local ttl=tonumber(ARGV[4]) "
        "if ttl and ttl>0 then redis.call('pexpire',KEYS[1],ttl) end "
        "return 1");
    if (!validateKey(key, error) || identity.isEmpty()
        || ttlMilliseconds < 0
        || (type != QByteArrayLiteral("hash")
            && type != QByteArrayLiteral("list")
            && type != QByteArrayLiteral("set")
            && type != QByteArrayLiteral("zset")
            && type != QByteArrayLiteral("stream"))) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis 集合键的类型、初始条目或 TTL 无效。");
        return false;
    }
    qint64 created = 0;
    if (!expectInteger(
            {QByteArrayLiteral("EVAL"), script, QByteArrayLiteral("1"),
             key, type, identity, value,
             QByteArray::number(ttlMilliseconds)},
            &created, error)) {
        return false;
    }
    if (created != 1) {
        assignError(error, QStringLiteral("同名 Redis 键已经存在；未覆盖现有数据。"));
        return false;
    }
    return true;
}

bool RedisSession::updateString(const QByteArray &key,
                                const QByteArray &value, QString *error)
{
    if (!validateKey(key, error))
        return false;
    RedisReply reply;
    if (!sendCommand({QByteArrayLiteral("SET"), key, value,
                      QByteArrayLiteral("XX"), QByteArrayLiteral("KEEPTTL")},
                     &reply, error)) {
        return false;
    }
    if (reply.type == RedisReply::Type::Null) {
        assignError(error, QStringLiteral("Redis 键已不存在；未重新创建。"));
        return false;
    }
    return replyIsStatus(reply, QByteArrayLiteral("OK"));
}

bool RedisSession::renameKey(const QByteArray &key, const QByteArray &newKey,
                             QString *error)
{
    if (!validateKey(key, error) || !validateKey(newKey, error))
        return false;
    qint64 renamed = 0;
    if (!expectInteger({QByteArrayLiteral("RENAMENX"), key, newKey},
                       &renamed, error)) {
        return false;
    }
    if (renamed != 1) {
        assignError(error, QStringLiteral("目标 Redis 键已存在；未覆盖任何数据。"));
        return false;
    }
    return true;
}

bool RedisSession::deleteKey(const QByteArray &key, QString *error)
{
    if (!validateKey(key, error))
        return false;
    qint64 removed = 0;
    QString unlinkError;
    if (!expectInteger({QByteArrayLiteral("UNLINK"), key},
                       &removed, &unlinkError)) {
        if (!unlinkError.contains(QStringLiteral("unknown command"),
                                  Qt::CaseInsensitive)) {
            assignError(error, unlinkError);
            return false;
        }
        if (!expectInteger({QByteArrayLiteral("DEL"), key}, &removed, error))
            return false;
    }
    if (removed != 1) {
        assignError(error, QStringLiteral("Redis 键已不存在。"));
        return false;
    }
    return true;
}

bool RedisSession::addEntry(const QByteArray &type, const QByteArray &key,
                            const QByteArray &identity,
                            const QByteArray &value, QString *error)
{
    if (!validateKey(key, error)
        || (identity.isEmpty() && type != QByteArrayLiteral("list"))) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis 字段或成员不能为空。");
        return false;
    }
    QVector<QByteArray> command;
    if (type == QByteArrayLiteral("hash"))
        command = {QByteArrayLiteral("HSETNX"), key, identity, value};
    else if (type == QByteArrayLiteral("list"))
        command = {QByteArrayLiteral("RPUSH"), key, identity};
    else if (type == QByteArrayLiteral("set"))
        command = {QByteArrayLiteral("SADD"), key, identity};
    else if (type == QByteArrayLiteral("zset"))
        command = {QByteArrayLiteral("ZADD"), key, QByteArrayLiteral("NX"),
                   value, identity};
    else if (type == QByteArrayLiteral("stream"))
        command = {QByteArrayLiteral("XADD"), key, QByteArrayLiteral("*"),
                   identity, value};
    else {
        assignError(error, QStringLiteral("该 Redis 类型不支持添加条目。"));
        return false;
    }
    RedisReply reply;
    if (!sendCommand(command, &reply, error))
        return false;
    if (type == QByteArrayLiteral("hash")
        || type == QByteArrayLiteral("set")
        || type == QByteArrayLiteral("zset")) {
        bool ok = false;
        const qint64 added = integerReply(reply, &ok);
        if (!ok) {
            assignError(error, QStringLiteral("Redis 返回了无效的新增响应。"));
            return false;
        }
        if (added == 0) {
            assignError(error, QStringLiteral("同名 Redis 字段或成员已经存在；未覆盖现有数据。"));
            return false;
        }
    }
    return true;
}

bool RedisSession::updateEntry(const QByteArray &type, const QByteArray &key,
                               const QByteArray &identity,
                               const QByteArray &value, QString *error)
{
    if (!validateKey(key, error) || identity.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis 条目标识不能为空。");
        return false;
    }
    QVector<QByteArray> command;
    if (type == QByteArrayLiteral("hash"))
        command = {QByteArrayLiteral("HSET"), key, identity, value};
    else if (type == QByteArrayLiteral("list"))
        command = {QByteArrayLiteral("LSET"), key, identity, value};
    else if (type == QByteArrayLiteral("zset"))
        command = {QByteArrayLiteral("ZADD"), key, QByteArrayLiteral("XX"),
                   value, identity};
    else {
        assignError(error, QStringLiteral("该 Redis 类型不支持原位修改条目。"));
        return false;
    }
    RedisReply reply;
    return sendCommand(command, &reply, error);
}

bool RedisSession::deleteEntry(const QByteArray &type, const QByteArray &key,
                               const QByteArray &identity, QString *error)
{
    if (!validateKey(key, error) || identity.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Redis 条目标识不能为空。");
        return false;
    }
    QVector<QByteArray> command;
    if (type == QByteArrayLiteral("hash"))
        command = {QByteArrayLiteral("HDEL"), key, identity};
    else if (type == QByteArrayLiteral("set"))
        command = {QByteArrayLiteral("SREM"), key, identity};
    else if (type == QByteArrayLiteral("zset"))
        command = {QByteArrayLiteral("ZREM"), key, identity};
    else if (type == QByteArrayLiteral("stream"))
        command = {QByteArrayLiteral("XDEL"), key, identity};
    else {
        assignError(error, QStringLiteral("该 Redis 类型不支持安全删除单个条目。"));
        return false;
    }
    qint64 removed = 0;
    if (!expectInteger(command, &removed, error))
        return false;
    if (removed < 1) {
        assignError(error, QStringLiteral("Redis 条目已不存在。"));
        return false;
    }
    return true;
}

bool RedisSession::sendCommand(const QVector<QByteArray> &arguments,
                               RedisReply *reply, QString *error)
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) {
        assignError(error, QStringLiteral("Redis 尚未连接。"));
        return false;
    }
    if (arguments.isEmpty() || arguments.size() > MaximumCommandArguments) {
        assignError(error, QStringLiteral("Redis 命令参数数量超过安全限制。"));
        return false;
    }
    qint64 commandBytes = 0;
    for (const QByteArray &argument : arguments) {
        commandBytes += argument.size();
        if (commandBytes > MaximumCommandBytes) {
            assignError(error, QStringLiteral("Redis 命令数据超过 8 MiB 安全限制。"));
            return false;
        }
    }

    const QByteArray command = RedisProtocol::encodeCommand(arguments);
    if (socket_->write(command) != command.size()) {
        assignError(error, QStringLiteral("Redis 命令写入失败：%1")
                               .arg(socket_->errorString()));
        return false;
    }
    const int timeout = qBound(1, config_.connectTimeoutSeconds, 60) * 1000;
    if (!socket_->waitForBytesWritten(timeout)) {
        assignError(error, QStringLiteral("Redis 命令写入超时：%1")
                               .arg(socket_->errorString()));
        return false;
    }

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        buffer.append(socket_->readAll());
        if (buffer.size() > MaximumReplyBufferBytes) {
            assignError(error, QStringLiteral("Redis 响应超过 16 MiB 会话限制。"));
            resetSocket();
            return false;
        }

        qsizetype offset = 0;
        RedisReply parsed;
        QString parseError;
        const RedisProtocol::ParseStatus status =
            RedisProtocol::parseReply(buffer, &offset, &parsed, &parseError);
        if (status == RedisProtocol::ParseStatus::Complete) {
            if (parsed.type == RedisReply::Type::Error) {
                assignError(error, QStringLiteral("Redis 返回错误：%1")
                                       .arg(QString::fromUtf8(parsed.bytes)));
                return false;
            }
            if (reply)
                *reply = std::move(parsed);
            if (error)
                error->clear();
            return true;
        }
        if (status == RedisProtocol::ParseStatus::Error) {
            assignError(error, parseError);
            resetSocket();
            return false;
        }

        const int remaining = timeout - static_cast<int>(timer.elapsed());
        if (remaining <= 0 || !socket_->waitForReadyRead(remaining))
            break;
    }
    assignError(error, QStringLiteral("等待 Redis 响应超时：%1")
                           .arg(socket_->errorString()));
    return false;
}

bool RedisSession::expectInteger(const QVector<QByteArray> &arguments,
                                 qint64 *value, QString *error)
{
    RedisReply reply;
    if (!sendCommand(arguments, &reply, error))
        return false;
    bool ok = false;
    const qint64 parsed = integerReply(reply, &ok);
    if (!ok) {
        assignError(error, QStringLiteral("Redis 返回了无效的整数响应。"));
        return false;
    }
    if (value)
        *value = parsed;
    return true;
}

bool RedisSession::validateKey(const QByteArray &key, QString *error) const
{
    if (key.isEmpty()) {
        assignError(error, QStringLiteral("Redis 键不能为空。"));
        return false;
    }
    if (key.size() > 1024 * 1024) {
        assignError(error, QStringLiteral("Redis 键超过 1 MiB 安全限制。"));
        return false;
    }
    return true;
}

void RedisSession::resetSocket()
{
    if (!socket_)
        return;
    socket_->disconnectFromHost();
    if (socket_->state() != QAbstractSocket::UnconnectedState)
        socket_->waitForDisconnected(100);
    socket_.reset();
}

} // namespace vsdb

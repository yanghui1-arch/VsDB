#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace vsdb {

struct RedisReply
{
    enum class Type
    {
        SimpleString,
        Error,
        Integer,
        BulkString,
        Array,
        Null
    };

    Type type = Type::Null;
    QByteArray bytes;
    qint64 integer = 0;
    QVector<RedisReply> elements;
};

class RedisProtocol final
{
public:
    enum class ParseStatus
    {
        Complete,
        Incomplete,
        Error
    };

    static QByteArray encodeCommand(const QVector<QByteArray> &arguments);
    static ParseStatus parseReply(const QByteArray &buffer, qsizetype *offset,
                                  RedisReply *reply, QString *error,
                                  qint64 maximumBulkBytes = 8LL * 1024 * 1024,
                                  qsizetype maximumArrayItems = 10000);
};

bool redisBytesAreText(const QByteArray &value);
QString redisDisplayBytes(const QByteArray &value, qsizetype maximumCharacters = 4096);

} // namespace vsdb

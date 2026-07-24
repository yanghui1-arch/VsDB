#include "database/redis/RedisProtocol.h"

#include <QStringDecoder>

#include <limits>

namespace vsdb {

namespace {

constexpr qsizetype MaximumHeaderBytes = 8192;
constexpr int MaximumNestingDepth = 32;

RedisProtocol::ParseStatus parseLine(const QByteArray &buffer, qsizetype *offset,
                                     QByteArray *line, QString *error)
{
    const qsizetype lineEnd = buffer.indexOf("\r\n", *offset);
    if (lineEnd < 0) {
        if (buffer.size() - *offset > MaximumHeaderBytes) {
            if (error)
                *error = QStringLiteral("Redis 响应头超过安全限制。");
            return RedisProtocol::ParseStatus::Error;
        }
        return RedisProtocol::ParseStatus::Incomplete;
    }
    if (lineEnd - *offset > MaximumHeaderBytes) {
        if (error)
            *error = QStringLiteral("Redis 响应头超过安全限制。");
        return RedisProtocol::ParseStatus::Error;
    }
    *line = buffer.mid(*offset, lineEnd - *offset);
    *offset = lineEnd + 2;
    return RedisProtocol::ParseStatus::Complete;
}

RedisProtocol::ParseStatus parseReplyInternal(
    const QByteArray &buffer, qsizetype *offset, RedisReply *reply,
    QString *error, qint64 maximumBulkBytes, qsizetype maximumArrayItems,
    int depth)
{
    if (depth > MaximumNestingDepth) {
        if (error)
            *error = QStringLiteral("Redis 响应嵌套层级超过安全限制。");
        return RedisProtocol::ParseStatus::Error;
    }
    if (*offset >= buffer.size())
        return RedisProtocol::ParseStatus::Incomplete;

    const char prefix = buffer.at((*offset)++);
    QByteArray line;
    RedisProtocol::ParseStatus status =
        parseLine(buffer, offset, &line, error);
    if (status != RedisProtocol::ParseStatus::Complete)
        return status;

    RedisReply parsed;
    switch (prefix) {
    case '+':
        parsed.type = RedisReply::Type::SimpleString;
        parsed.bytes = std::move(line);
        break;
    case '-':
        parsed.type = RedisReply::Type::Error;
        parsed.bytes = std::move(line);
        break;
    case ':': {
        bool ok = false;
        const qint64 value = line.toLongLong(&ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("Redis 整数响应格式无效。");
            return RedisProtocol::ParseStatus::Error;
        }
        parsed.type = RedisReply::Type::Integer;
        parsed.integer = value;
        break;
    }
    case '$': {
        bool ok = false;
        const qint64 length = line.toLongLong(&ok);
        if (!ok || length < -1) {
            if (error)
                *error = QStringLiteral("Redis Bulk String 长度无效。");
            return RedisProtocol::ParseStatus::Error;
        }
        if (length == -1) {
            parsed.type = RedisReply::Type::Null;
            break;
        }
        if (length > maximumBulkBytes
            || length > std::numeric_limits<qsizetype>::max()) {
            if (error) {
                *error = QStringLiteral("Redis 数据超过 %1 MiB 的单次读取限制。")
                             .arg(maximumBulkBytes / (1024 * 1024));
            }
            return RedisProtocol::ParseStatus::Error;
        }
        const qsizetype byteLength = static_cast<qsizetype>(length);
        if (byteLength > buffer.size() - *offset
            || buffer.size() - *offset - byteLength < 2) {
            return RedisProtocol::ParseStatus::Incomplete;
        }
        if (buffer.mid(*offset + byteLength, 2) != QByteArrayLiteral("\r\n")) {
            if (error)
                *error = QStringLiteral("Redis Bulk String 结尾无效。");
            return RedisProtocol::ParseStatus::Error;
        }
        parsed.type = RedisReply::Type::BulkString;
        parsed.bytes = buffer.mid(*offset, byteLength);
        *offset += byteLength + 2;
        break;
    }
    case '*': {
        bool ok = false;
        const qint64 count = line.toLongLong(&ok);
        if (!ok || count < -1 || count > maximumArrayItems) {
            if (error)
                *error = QStringLiteral("Redis 数组大小无效或超过安全限制。");
            return RedisProtocol::ParseStatus::Error;
        }
        if (count == -1) {
            parsed.type = RedisReply::Type::Null;
            break;
        }
        parsed.type = RedisReply::Type::Array;
        parsed.elements.reserve(static_cast<qsizetype>(count));
        for (qint64 index = 0; index < count; ++index) {
            RedisReply child;
            status = parseReplyInternal(
                buffer, offset, &child, error, maximumBulkBytes,
                maximumArrayItems, depth + 1);
            if (status != RedisProtocol::ParseStatus::Complete)
                return status;
            parsed.elements.append(std::move(child));
        }
        break;
    }
    default:
        if (error)
            *error = QStringLiteral("Redis 响应使用了不支持的 RESP 类型。");
        return RedisProtocol::ParseStatus::Error;
    }

    *reply = std::move(parsed);
    return RedisProtocol::ParseStatus::Complete;
}

} // namespace

QByteArray RedisProtocol::encodeCommand(const QVector<QByteArray> &arguments)
{
    QByteArray encoded;
    encoded.reserve(64);
    encoded.append('*');
    encoded.append(QByteArray::number(arguments.size()));
    encoded.append("\r\n");
    for (const QByteArray &argument : arguments) {
        encoded.append('$');
        encoded.append(QByteArray::number(argument.size()));
        encoded.append("\r\n");
        encoded.append(argument);
        encoded.append("\r\n");
    }
    return encoded;
}

RedisProtocol::ParseStatus RedisProtocol::parseReply(
    const QByteArray &buffer, qsizetype *offset, RedisReply *reply,
    QString *error, qint64 maximumBulkBytes, qsizetype maximumArrayItems)
{
    if (!offset || !reply || *offset < 0 || *offset > buffer.size()) {
        if (error)
            *error = QStringLiteral("Redis 响应解析参数无效。");
        return ParseStatus::Error;
    }
    const qsizetype originalOffset = *offset;
    RedisReply parsed;
    const ParseStatus status = parseReplyInternal(
        buffer, offset, &parsed, error, maximumBulkBytes,
        maximumArrayItems, 0);
    if (status != ParseStatus::Complete) {
        *offset = originalOffset;
        return status;
    }
    *reply = std::move(parsed);
    return ParseStatus::Complete;
}

bool redisBytesAreText(const QByteArray &value)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(value);
    if (decoder.hasError() || text.toUtf8() != value)
        return false;
    for (const QChar character : text) {
        if (character.isNull())
            return false;
        if (character.category() == QChar::Other_Control
            && character != QLatin1Char('\n')
            && character != QLatin1Char('\r')
            && character != QLatin1Char('\t')) {
            return false;
        }
    }
    return true;
}

QString redisDisplayBytes(const QByteArray &value, qsizetype maximumCharacters)
{
    QString result;
    if (redisBytesAreText(value)) {
        result = QString::fromUtf8(value);
    } else {
        result = QStringLiteral("0x%1").arg(QString::fromLatin1(value.toHex()));
    }
    if (maximumCharacters >= 0 && result.size() > maximumCharacters) {
        result.truncate(maximumCharacters);
        result.append(QStringLiteral("…"));
    }
    return result;
}

} // namespace vsdb

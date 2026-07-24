#include "database/redis/RedisProtocol.h"
#include "database/redis/RedisSession.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <future>
#include <iostream>
#include <thread>

namespace {

int fail(int code, const char *message)
{
    std::cerr << message << '\n';
    return code;
}

QByteArray bulkReply(const QByteArray &value)
{
    return QByteArrayLiteral("$") + QByteArray::number(value.size())
        + QByteArrayLiteral("\r\n") + value + QByteArrayLiteral("\r\n");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QByteArray binaryValue("a\0b\r\nc", 6);
    const QByteArray encoded = vsdb::RedisProtocol::encodeCommand(
        {QByteArrayLiteral("SET"), QByteArrayLiteral("line\r\nkey"),
         binaryValue});
    QByteArray expected =
        QByteArrayLiteral("*3\r\n$3\r\nSET\r\n$9\r\nline\r\nkey\r\n"
                          "$6\r\n");
    expected.append(binaryValue);
    expected.append("\r\n");
    if (encoded != expected)
        return fail(1, "RESP command encoding is not binary safe");

    const QByteArray scanReply =
        QByteArrayLiteral("*2\r\n$2\r\n42\r\n*2\r\n$5\r\nalpha\r\n$4\r\nbeta\r\n");
    for (qsizetype available = 0; available < scanReply.size(); ++available) {
        qsizetype offset = 0;
        vsdb::RedisReply reply;
        QString error;
        if (vsdb::RedisProtocol::parseReply(
                scanReply.left(available), &offset, &reply, &error)
            != vsdb::RedisProtocol::ParseStatus::Incomplete
            || offset != 0) {
            return fail(2, "partial RESP response was not retained safely");
        }
    }

    qsizetype offset = 0;
    vsdb::RedisReply parsed;
    QString error;
    if (vsdb::RedisProtocol::parseReply(
            scanReply, &offset, &parsed, &error)
            != vsdb::RedisProtocol::ParseStatus::Complete
        || offset != scanReply.size()
        || parsed.type != vsdb::RedisReply::Type::Array
        || parsed.elements.size() != 2
        || parsed.elements.at(1).elements.size() != 2
        || parsed.elements.at(1).elements.constLast().bytes
            != QByteArrayLiteral("beta")) {
        return fail(3, "nested RESP response was parsed incorrectly");
    }

    offset = 0;
    if (vsdb::RedisProtocol::parseReply(
            QByteArrayLiteral("$9000000\r\n"), &offset, &parsed, &error)
        != vsdb::RedisProtocol::ParseStatus::Error) {
        return fail(4, "oversized RESP bulk response was not rejected");
    }

    offset = 0;
    if (vsdb::RedisProtocol::parseReply(
            QByteArrayLiteral("*10001\r\n"), &offset, &parsed, &error)
        != vsdb::RedisProtocol::ParseStatus::Error) {
        return fail(5, "oversized RESP array was not rejected");
    }

    if (!vsdb::redisBytesAreText(QByteArrayLiteral("Nexus 数据"))
        || vsdb::redisBytesAreText(QByteArray("bad\0value", 9))
        || !vsdb::redisDisplayBytes(QByteArray("\xFF", 1)).startsWith(
            QStringLiteral("0x"))) {
        return fail(6, "binary/text display classification is inaccurate");
    }

    std::promise<quint16> portPromise;
    std::future<quint16> portFuture = portPromise.get_future();
    QVector<QVector<QByteArray>> observedCommands;
    QString fakeServerError;
    std::jthread fakeServer(
        [&](std::stop_token) {
            QTcpServer server;
            if (!server.listen(QHostAddress::LocalHost, 0)) {
                fakeServerError = server.errorString();
                portPromise.set_value(0);
                return;
            }
            portPromise.set_value(server.serverPort());
            if (!server.waitForNewConnection(5000)) {
                fakeServerError = QStringLiteral("client connection timed out");
                return;
            }
            std::unique_ptr<QTcpSocket> client(server.nextPendingConnection());
            QByteArray requestBuffer;
            bool complete = false;
            while (!complete
                   && client->state() == QAbstractSocket::ConnectedState) {
                if (client->bytesAvailable() == 0
                    && !client->waitForReadyRead(5000)) {
                    fakeServerError =
                        QStringLiteral("command read timed out");
                    break;
                }
                requestBuffer.append(client->readAll());
                qsizetype requestOffset = 0;
                vsdb::RedisReply request;
                QString parseError;
                const auto status = vsdb::RedisProtocol::parseReply(
                    requestBuffer, &requestOffset, &request, &parseError);
                if (status == vsdb::RedisProtocol::ParseStatus::Incomplete)
                    continue;
                if (status == vsdb::RedisProtocol::ParseStatus::Error
                    || request.type != vsdb::RedisReply::Type::Array) {
                    fakeServerError =
                        parseError.isEmpty()
                            ? QStringLiteral("invalid command")
                            : parseError;
                    break;
                }
                requestBuffer.remove(0, requestOffset);

                QVector<QByteArray> command;
                command.reserve(request.elements.size());
                for (const vsdb::RedisReply &argument : request.elements)
                    command.append(argument.bytes);
                observedCommands.append(command);

                const QByteArray name =
                    command.isEmpty() ? QByteArray{} : command.constFirst().toUpper();
                QByteArray response;
                if (name == QByteArrayLiteral("AUTH")
                    || name == QByteArrayLiteral("SELECT")) {
                    response = QByteArrayLiteral("+OK\r\n");
                } else if (name == QByteArrayLiteral("PING")) {
                    response = QByteArrayLiteral("+PONG\r\n");
                } else if (name == QByteArrayLiteral("INFO")) {
                    response = bulkReply(
                        QByteArrayLiteral("# Keyspace\r\n"
                                          "db2:keys=1,expires=1,avg_ttl=5000\r\n"));
                } else if (name == QByteArrayLiteral("SET")) {
                    response = QByteArrayLiteral("+OK\r\n");
                } else if (name == QByteArrayLiteral("TYPE")) {
                    response = QByteArrayLiteral("+string\r\n");
                } else if (name == QByteArrayLiteral("PTTL")) {
                    response = QByteArrayLiteral(":5000\r\n");
                } else if (name == QByteArrayLiteral("STRLEN")) {
                    response = QByteArrayLiteral(":6\r\n");
                } else if (name == QByteArrayLiteral("GETRANGE")) {
                    response = bulkReply(binaryValue);
                } else if (name == QByteArrayLiteral("UNLINK")) {
                    response = QByteArrayLiteral(":1\r\n");
                    complete = true;
                } else {
                    response =
                        QByteArrayLiteral("-ERR unexpected command\r\n");
                    fakeServerError =
                        QStringLiteral("unexpected command: %1")
                            .arg(QString::fromLatin1(name));
                    complete = true;
                }
                client->write(response);
                if (!client->waitForBytesWritten(5000)) {
                    fakeServerError =
                        QStringLiteral("response write timed out");
                    break;
                }
            }
        });

    const quint16 fakePort = portFuture.get();
    if (fakePort == 0)
        return fail(7, "fake Redis server failed to listen");

    vsdb::RedisConnectionConfig fakeConfig;
    fakeConfig.host = QStringLiteral("127.0.0.1");
    fakeConfig.port = fakePort;
    fakeConfig.username = QStringLiteral("nexus");
    fakeConfig.password = QStringLiteral("server secret");
    fakeConfig.database = 2;
    fakeConfig.connectTimeoutSeconds = 5;

    vsdb::RedisSession fakeSession;
    if (!fakeSession.connectToServer(fakeConfig, &error))
        return fail(8, "Redis session handshake failed");
    const QVector<vsdb::RedisDatabaseInfo> fakeDatabases =
        fakeSession.databases(&error);
    if (!error.isEmpty() || fakeDatabases.size() != 1
        || fakeDatabases.constFirst().index != 2
        || fakeDatabases.constFirst().keys != 1)
        return fail(9, "Redis keyspace discovery failed");
    const QByteArray fakeKey =
        QByteArrayLiteral("vsdb:test:key\r\nbinary-safe");
    if (!fakeSession.createString(fakeKey, binaryValue, 60000, &error))
        return fail(10, "Redis safe create failed");
    const vsdb::RedisKeyDetails fakeDetails =
        fakeSession.inspectKey(fakeKey, 10, 1024, &error);
    if (!error.isEmpty()
        || fakeDetails.type != QByteArrayLiteral("string")
        || fakeDetails.stringValue != binaryValue
        || fakeDetails.size != binaryValue.size()
        || fakeDetails.ttlMilliseconds != 5000)
        return fail(11, "Redis bounded inspection failed");
    if (!fakeSession.updateString(
            fakeKey, QByteArrayLiteral("updated"), &error))
        return fail(12, "Redis safe update failed");
    if (!fakeSession.deleteKey(fakeKey, &error))
        return fail(13, "Redis asynchronous delete failed");
    fakeSession.disconnect();
    fakeServer.join();
    if (!fakeServerError.isEmpty())
        return fail(14, "fake Redis server observed an invalid command");
    if (observedCommands.size() < 11
        || observedCommands.constFirst()
            != QVector<QByteArray>{QByteArrayLiteral("AUTH"),
                                   QByteArrayLiteral("nexus"),
                                   QByteArrayLiteral("server secret")}
        || observedCommands.at(1)
            != QVector<QByteArray>{QByteArrayLiteral("SELECT"),
                                   QByteArrayLiteral("2")}
        || observedCommands.at(5).at(1) != fakeKey) {
        return fail(15, "Redis commands were not encoded as exact arguments");
    }

    const QString redisHost = qEnvironmentVariable("VSDB_TEST_REDIS_HOST");
    if (redisHost.isEmpty()) {
        std::cout << "VSDB_TEST_REDIS_HOST is not set; integration test skipped\n";
        return 0;
    }

    vsdb::RedisConnectionConfig config;
    config.host = redisHost;
    config.port = qEnvironmentVariableIntValue("VSDB_TEST_REDIS_PORT");
    if (config.port <= 0)
        config.port = 6379;
    config.username = qEnvironmentVariable("VSDB_TEST_REDIS_USER");
    config.password = qEnvironmentVariable("VSDB_TEST_REDIS_PASSWORD");
    config.database = qEnvironmentVariableIntValue("VSDB_TEST_REDIS_DATABASE");
    config.tls = qEnvironmentVariableIntValue("VSDB_TEST_REDIS_TLS") == 1;

    vsdb::RedisSession session;
    if (!session.connectToServer(config, &error))
        return fail(16, "Redis integration connection failed");

    const QByteArray key =
        QByteArrayLiteral("vsdb:integration:binary-safe");
    session.deleteKey(key, nullptr);
    if (!session.createString(key, binaryValue, 60000, &error))
        return fail(17, "Redis integration create failed");
    const vsdb::RedisKeyDetails details =
        session.inspectKey(key, 10, 1024, &error);
    if (!error.isEmpty() || details.type != QByteArrayLiteral("string")
        || details.stringValue != binaryValue || details.ttlMilliseconds <= 0)
        return fail(18, "Redis integration read was inaccurate");
    if (!session.updateString(key, QByteArrayLiteral("updated"), &error))
        return fail(19, "Redis integration update failed");
    if (!session.deleteKey(key, &error))
        return fail(20, "Redis integration delete failed");

    return 0;
}

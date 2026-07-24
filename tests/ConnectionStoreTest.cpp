#include "database/ConnectionStore.h"

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

    QSettings settings(temporaryDirectory.filePath(QStringLiteral("connections.ini")),
                       QSettings::IniFormat);
    MemoryCredentialStore credentials;
    vsdb::SavedConnection connection;
    connection.config.host = QStringLiteral("db.internal.example");
    connection.config.port = 5544;
    connection.config.user = QStringLiteral("analytics");
    connection.config.password = QStringLiteral("correct horse battery staple");
    connection.config.database = QStringLiteral("warehouse");
    connection.config.sslMode = QStringLiteral("require");
    connection.config.connectTimeoutSeconds = 17;

    QString error;
    if (!vsdb::ConnectionStore::upsert(settings, credentials, connection, &error))
        return fail(2, "saving a connection failed");
    if (connection.id.isEmpty() || !connection.hasStoredPassword)
        return fail(3, "saved connection identity or password state is invalid");

    connection.snapshot.users = {QStringLiteral("analytics"),
                                 QStringLiteral("reporter")};
    connection.snapshot.databases = {QStringLiteral("warehouse"),
                                     QStringLiteral("reporting")};
    connection.snapshot.schemas = {QStringLiteral("public"),
                                   QStringLiteral("information_schema")};
    connection.snapshot.publicTables = {QStringLiteral("orders"),
                                        QStringLiteral("customers")};
    connection.snapshot.publicViews = {QStringLiteral("daily_revenue")};
    if (!vsdb::ConnectionStore::saveSnapshot(settings, connection, &error))
        return fail(4, "saving a connection snapshot failed");

    for (const QString &key : settings.allKeys()) {
        if (settings.value(key).toString() == connection.config.password)
            return fail(5, "password leaked into QSettings");
    }

    QStringList warnings;
    const QVector<vsdb::SavedConnection> loaded =
        vsdb::ConnectionStore::load(settings, credentials, &warnings);
    if (!warnings.isEmpty() || loaded.size() != 1)
        return fail(6, "saved connection did not load cleanly");
    const vsdb::SavedConnection &restored = loaded.constFirst();
    if (restored.id != connection.id
        || restored.config.host != connection.config.host
        || restored.config.port != connection.config.port
        || restored.config.user != connection.config.user
        || restored.config.password != connection.config.password
        || restored.config.database != connection.config.database
        || restored.config.sslMode != connection.config.sslMode
        || restored.config.connectTimeoutSeconds
            != connection.config.connectTimeoutSeconds
        || restored.snapshot.users != connection.snapshot.users
        || restored.snapshot.databases != connection.snapshot.databases
        || restored.snapshot.schemas != connection.snapshot.schemas
        || restored.snapshot.publicTables != connection.snapshot.publicTables
        || restored.snapshot.publicViews != connection.snapshot.publicViews
        || !restored.hasStoredPassword) {
        return fail(7, "saved connection fields were not restored");
    }

    connection.config.password = QStringLiteral("replacement password");
    connection.config.port = 6432;
    connection.config.database = QStringLiteral("nexus");
    if (!vsdb::ConnectionStore::upsert(settings, credentials, connection, &error))
        return fail(8, "updating a connection failed");
    if (settings.value(QStringLiteral("connections/order")).toStringList().size() != 1)
        return fail(9, "updating duplicated the connection");

    const QVector<vsdb::SavedConnection> updated =
        vsdb::ConnectionStore::load(settings, credentials);
    if (updated.size() != 1
        || updated.constFirst().config.password != connection.config.password
        || updated.constFirst().config.port != 6432
        || updated.constFirst().config.database != QStringLiteral("nexus")
        || updated.constFirst().snapshot.publicTables
            != connection.snapshot.publicTables) {
        return fail(10, "updated connection was not restored");
    }

    MemoryCredentialStore missingCredentials;
    warnings.clear();
    const QVector<vsdb::SavedConnection> missingPassword =
        vsdb::ConnectionStore::load(settings, missingCredentials, &warnings);
    if (missingPassword.size() != 1
        || missingPassword.constFirst().hasStoredPassword
        || warnings.size() != 1) {
        return fail(11, "a connection with a missing password should remain visible");
    }

    vsdb::SavedConnection secondConnection;
    secondConnection.config.host = QStringLiteral("reporting.internal.example");
    secondConnection.config.user = QStringLiteral("reporter");
    secondConnection.config.password = QStringLiteral("second password");
    secondConnection.config.database = QStringLiteral("reporting");
    if (!vsdb::ConnectionStore::upsert(
            settings, credentials, secondConnection, &error)) {
        return fail(12, "saving a second connection failed");
    }
    const QVector<vsdb::SavedConnection> multipleConnections =
        vsdb::ConnectionStore::load(settings, credentials);
    if (multipleConnections.size() != 2
        || multipleConnections.at(0).id != connection.id
        || multipleConnections.at(1).id != secondConnection.id) {
        return fail(13, "multiple connections were not restored in saved order");
    }

    if (!vsdb::ConnectionStore::remove(
            settings, credentials, connection.id, &error)) {
        return fail(14, "removing a connection failed");
    }
    const QVector<vsdb::SavedConnection> remaining =
        vsdb::ConnectionStore::load(settings, credentials);
    if (remaining.size() != 1 || remaining.constFirst().id != secondConnection.id) {
        return fail(15, "removing one connection affected another connection");
    }
    if (!vsdb::ConnectionStore::remove(
            settings, credentials, secondConnection.id, &error)) {
        return fail(16, "removing the second connection failed");
    }
    if (!vsdb::ConnectionStore::load(settings, credentials).isEmpty()
        || !credentials.secrets.isEmpty()) {
        return fail(17, "connection metadata or password remained after removal");
    }

    return 0;
}

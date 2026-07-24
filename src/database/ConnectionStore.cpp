#include "database/ConnectionStore.h"

#include <QSettings>
#include <QStringList>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace vsdb {

namespace {

void assignError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString profileGroup(const QString &connectionId)
{
    return QStringLiteral("connections/profiles/%1").arg(connectionId);
}

#ifdef Q_OS_WIN

class WindowsCredentialStore final : public CredentialStore
{
public:
    bool write(const QString &key, const QString &secret, QString *error) override
    {
        std::wstring target = key.toStdWString();
        std::wstring comment = QStringLiteral("VsDB PostgreSQL connection password").toStdWString();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.Comment = comment.data();
        credential.CredentialBlobSize =
            static_cast<DWORD>(secret.size() * static_cast<qsizetype>(sizeof(wchar_t)));
        credential.CredentialBlob = secret.isEmpty()
            ? nullptr
            : reinterpret_cast<LPBYTE>(const_cast<ushort *>(secret.utf16()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = target.data();
        if (CredWriteW(&credential, 0))
            return true;
        assignError(error, QStringLiteral("无法写入 Windows 凭据管理器（错误 %1）。")
                               .arg(GetLastError()));
        return false;
    }

    bool read(const QString &key, QString *secret, QString *error) const override
    {
        std::wstring target = key.toStdWString();
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            const DWORD code = GetLastError();
            if (code != ERROR_NOT_FOUND) {
                assignError(error, QStringLiteral("无法读取 Windows 凭据管理器（错误 %1）。")
                                       .arg(code));
            }
            return false;
        }
        const std::unique_ptr<CREDENTIALW, CredentialDeleter> storedCredential(credential);

        if (secret) {
            if (storedCredential->CredentialBlobSize == 0) {
                secret->clear();
            } else {
                const auto *characters = reinterpret_cast<const wchar_t *>(
                    storedCredential->CredentialBlob);
                *secret = QString::fromWCharArray(
                    characters,
                    static_cast<qsizetype>(
                        storedCredential->CredentialBlobSize / sizeof(wchar_t)));
            }
        }
        return true;
    }

    bool remove(const QString &key, QString *error) override
    {
        std::wstring target = key.toStdWString();
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
            return true;
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND)
            return true;
        assignError(error, QStringLiteral("无法从 Windows 凭据管理器删除密码（错误 %1）。")
                               .arg(code));
        return false;
    }

private:
    struct CredentialDeleter
    {
        void operator()(CREDENTIALW *credential) const
        {
            CredFree(credential);
        }
    };
};

#else

class UnavailableCredentialStore final : public CredentialStore
{
public:
    bool write(const QString &, const QString &, QString *error) override
    {
        assignError(error, unavailableMessage());
        return false;
    }

    bool read(const QString &, QString *, QString *error) const override
    {
        assignError(error, unavailableMessage());
        return false;
    }

    bool remove(const QString &, QString *error) override
    {
        assignError(error, unavailableMessage());
        return false;
    }

private:
    static QString unavailableMessage()
    {
        return QStringLiteral("当前平台尚未提供安全凭据存储，VsDB 不会把密码明文写入设置。");
    }
};

#endif

} // namespace

std::unique_ptr<CredentialStore> createPlatformCredentialStore()
{
#ifdef Q_OS_WIN
    return std::make_unique<WindowsCredentialStore>();
#else
    return std::make_unique<UnavailableCredentialStore>();
#endif
}

QVector<SavedConnection> ConnectionStore::load(
    QSettings &settings, const CredentialStore &credentials, QStringList *warnings)
{
    QVector<SavedConnection> result;
    const QStringList order =
        settings.value(QStringLiteral("connections/order")).toStringList();
    result.reserve(order.size());

    for (const QString &id : order) {
        if (id.isEmpty())
            continue;

        settings.beginGroup(profileGroup(id));
        const QString driver =
            settings.value(QStringLiteral("driver"), QStringLiteral("QPSQL")).toString();
        SavedConnection connection;
        connection.id = id;
        connection.config.host = settings.value(QStringLiteral("host")).toString();
        connection.config.port = settings.value(QStringLiteral("port"), 5432).toInt();
        connection.config.user = settings.value(QStringLiteral("user")).toString();
        connection.config.database = settings.value(QStringLiteral("database")).toString();
        connection.config.sslMode =
            settings.value(QStringLiteral("sslMode"), QStringLiteral("prefer")).toString();
        connection.config.connectTimeoutSeconds =
            settings.value(QStringLiteral("connectTimeout"), 10).toInt();
        connection.snapshot.users =
            settings.value(QStringLiteral("snapshot/users")).toStringList();
        connection.snapshot.databases =
            settings.value(QStringLiteral("snapshot/databases")).toStringList();
        connection.snapshot.schemas =
            settings.value(QStringLiteral("snapshot/schemas")).toStringList();
        connection.snapshot.publicTables =
            settings.value(QStringLiteral("snapshot/publicTables")).toStringList();
        connection.snapshot.publicViews =
            settings.value(QStringLiteral("snapshot/publicViews")).toStringList();
        settings.endGroup();

        if (driver != QStringLiteral("QPSQL") || connection.config.host.isEmpty()
            || connection.config.user.isEmpty() || connection.config.database.isEmpty()
            || connection.config.port < 1 || connection.config.port > 65535) {
            if (warnings) {
                warnings->append(
                    QStringLiteral("已跳过损坏的连接配置：%1").arg(id));
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

bool ConnectionStore::upsert(QSettings &settings, CredentialStore &credentials,
                             SavedConnection &connection, QString *error)
{
    if (connection.config.host.isEmpty() || connection.config.user.isEmpty()
        || connection.config.database.isEmpty() || connection.config.port < 1
        || connection.config.port > 65535) {
        assignError(error, QStringLiteral("连接地址、端口、用户或数据库无效。"));
        return false;
    }

    const bool isNew = connection.id.isEmpty();
    if (isNew)
        connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Write the secret first so metadata never points to an unsecured password.
    if (!credentials.write(credentialKey(connection.id),
                           connection.config.password, error)) {
        if (isNew)
            connection.id.clear();
        return false;
    }

    settings.beginGroup(profileGroup(connection.id));
    settings.setValue(QStringLiteral("driver"), QStringLiteral("QPSQL"));
    settings.setValue(QStringLiteral("host"), connection.config.host);
    settings.setValue(QStringLiteral("port"), connection.config.port);
    settings.setValue(QStringLiteral("user"), connection.config.user);
    settings.setValue(QStringLiteral("database"), connection.config.database);
    settings.setValue(QStringLiteral("sslMode"), connection.config.sslMode);
    settings.setValue(QStringLiteral("connectTimeout"),
                      connection.config.connectTimeoutSeconds);
    settings.endGroup();

    QStringList order =
        settings.value(QStringLiteral("connections/order")).toStringList();
    if (!order.contains(connection.id)) {
        order.append(connection.id);
        settings.setValue(QStringLiteral("connections/order"), order);
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        assignError(error, QStringLiteral("连接配置无法写入本地设置。"));
        if (isNew) {
            QString ignoredError;
            credentials.remove(credentialKey(connection.id), &ignoredError);
            connection.id.clear();
        }
        return false;
    }

    connection.hasStoredPassword = true;
    return true;
}

bool ConnectionStore::saveSnapshot(QSettings &settings,
                                   const SavedConnection &connection,
                                   QString *error)
{
    settings.beginGroup(profileGroup(connection.id));
    settings.setValue(QStringLiteral("snapshot/users"), connection.snapshot.users);
    settings.setValue(QStringLiteral("snapshot/databases"),
                      connection.snapshot.databases);
    settings.setValue(QStringLiteral("snapshot/schemas"), connection.snapshot.schemas);
    settings.setValue(QStringLiteral("snapshot/publicTables"),
                      connection.snapshot.publicTables);
    settings.setValue(QStringLiteral("snapshot/publicViews"),
                      connection.snapshot.publicViews);
    settings.endGroup();
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    assignError(error, QStringLiteral("数据库结构快照无法写入本地设置。"));
    return false;
}

bool ConnectionStore::remove(QSettings &settings, CredentialStore &credentials,
                             const QString &connectionId, QString *error)
{
    if (connectionId.isEmpty()) {
        assignError(error, QStringLiteral("没有选择要删除的连接。"));
        return false;
    }
    // Remove the secret first so a settings failure cannot leave an orphaned password.
    if (!credentials.remove(credentialKey(connectionId), error))
        return false;

    settings.remove(profileGroup(connectionId));
    QStringList order =
        settings.value(QStringLiteral("connections/order")).toStringList();
    order.removeAll(connectionId);
    settings.setValue(QStringLiteral("connections/order"), order);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        assignError(error, QStringLiteral("连接配置无法从本地设置中删除。"));
        return false;
    }
    return true;
}

QString ConnectionStore::credentialKey(const QString &connectionId)
{
    return QStringLiteral("VsDB/PostgreSQL/%1").arg(connectionId);
}

} // namespace vsdb

#include "ui/RedisConnectionDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace vsdb {

RedisConnectionDialog::RedisConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("连接 Redis"));
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel(
        QStringLiteral("支持 redis:// 与 rediss:// 地址。密码只保存到系统凭据存储；"
                       "启用 TLS 时会严格验证服务器证书。"),
        this);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("muted"));
    root->addWidget(hint);

    auto *form = new QFormLayout;
    address_ = new QLineEdit(QStringLiteral("localhost"), this);
    address_->setPlaceholderText(
        QStringLiteral("localhost 或 rediss://user@host:6379/0"));
    form->addRow(QStringLiteral("连接地址"), address_);

    port_ = new QSpinBox(this);
    port_->setRange(1, 65535);
    port_->setValue(6379);
    form->addRow(QStringLiteral("端口"), port_);

    username_ = new QLineEdit(this);
    username_->setPlaceholderText(QStringLiteral("可选；ACL 用户名"));
    form->addRow(QStringLiteral("用户名"), username_);

    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("密码"), password_);

    database_ = new QSpinBox(this);
    database_->setRange(0, 65535);
    form->addRow(QStringLiteral("数据库"), database_);

    tls_ = new QCheckBox(QStringLiteral("使用 TLS（校验证书）"), this);
    form->addRow(QStringLiteral("传输安全"), tls_);

    timeout_ = new QSpinBox(this);
    timeout_->setRange(1, 60);
    timeout_->setValue(10);
    timeout_->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("连接超时"), timeout_);
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *connectButton =
        buttons->addButton(QStringLiteral("连接"), QDialogButtonBox::AcceptRole);
    connect(connectButton, &QPushButton::clicked,
            this, &RedisConnectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void RedisConnectionDialog::setConfig(const RedisConnectionConfig &config)
{
    address_->setText(config.host);
    port_->setValue(config.port);
    username_->setText(config.username);
    password_->setText(config.password);
    database_->setValue(config.database);
    tls_->setChecked(config.tls);
    timeout_->setValue(config.connectTimeoutSeconds);
}

RedisConnectionConfig RedisConnectionDialog::config() const
{
    RedisConnectionConfig result;
    const QString address = address_->text().trimmed();
    result.host = address;
    result.port = port_->value();
    result.username = username_->text().trimmed();
    result.password = password_->text();
    result.database = database_->value();
    result.tls = tls_->isChecked();
    result.connectTimeoutSeconds = timeout_->value();

    if (address.startsWith(QStringLiteral("redis://"), Qt::CaseInsensitive)
        || address.startsWith(QStringLiteral("rediss://"), Qt::CaseInsensitive)) {
        const QUrl url(address);
        if (!url.host().isEmpty())
            result.host = url.host();
        if (url.port() > 0)
            result.port = url.port();
        if (!url.userName().isEmpty())
            result.username = url.userName();
        if (!url.password().isEmpty())
            result.password = url.password();
        bool databaseOk = false;
        const int database =
            url.path().mid(1).toInt(&databaseOk);
        if (databaseOk && database >= 0)
            result.database = database;
        result.tls = url.scheme().compare(
            QStringLiteral("rediss"), Qt::CaseInsensitive) == 0;
    }
    return result;
}

void RedisConnectionDialog::accept()
{
    const RedisConnectionConfig value = config();
    if (value.host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("连接参数不完整"),
                             QStringLiteral("Redis 连接地址不能为空。"));
        return;
    }
    QDialog::accept();
}

} // namespace vsdb

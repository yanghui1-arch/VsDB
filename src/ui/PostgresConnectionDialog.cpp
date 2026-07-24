#include "ui/PostgresConnectionDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace vsdb {

PostgresConnectionDialog::PostgresConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("连接 PostgreSQL"));
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral(
        "可填写主机名，也可直接粘贴 postgresql:// 连接地址。连接成功后，密码将安全保存到 Windows 凭据管理器。"),
        this);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("muted"));
    root->addWidget(hint);

    auto *form = new QFormLayout;
    address_ = new QLineEdit(this);
    address_->setPlaceholderText(QStringLiteral("localhost 或 postgresql://host/database"));
    form->addRow(QStringLiteral("连接地址"), address_);

    port_ = new QSpinBox(this);
    port_->setRange(1, 65535);
    port_->setValue(5432);
    form->addRow(QStringLiteral("端口"), port_);

    user_ = new QLineEdit(this);
    user_->setText(QStringLiteral("postgres"));
    form->addRow(QStringLiteral("用户"), user_);

    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("密码"), password_);

    database_ = new QLineEdit(this);
    database_->setText(QStringLiteral("postgres"));
    form->addRow(QStringLiteral("数据库"), database_);

    sslMode_ = new QComboBox(this);
    sslMode_->addItems({QStringLiteral("prefer"), QStringLiteral("require"),
                        QStringLiteral("verify-ca"), QStringLiteral("verify-full"),
                        QStringLiteral("disable"), QStringLiteral("allow")});
    form->addRow(QStringLiteral("SSL 模式"), sslMode_);

    timeout_ = new QSpinBox(this);
    timeout_->setRange(1, 60);
    timeout_->setValue(10);
    timeout_->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("连接超时"), timeout_);
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *connectButton = buttons->addButton(QStringLiteral("连接"),
                                                     QDialogButtonBox::AcceptRole);
    connect(connectButton, &QPushButton::clicked, this, &PostgresConnectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    address_->setText(QStringLiteral("localhost"));
}

void PostgresConnectionDialog::setConfig(const PostgresConnectionConfig &config)
{
    address_->setText(config.host);
    port_->setValue(config.port);
    user_->setText(config.user);
    password_->setText(config.password);
    database_->setText(config.database);
    sslMode_->setCurrentText(config.sslMode);
    timeout_->setValue(config.connectTimeoutSeconds);
}

PostgresConnectionConfig PostgresConnectionDialog::config() const
{
    PostgresConnectionConfig result;
    const QString address = address_->text().trimmed();
    result.host = address;
    result.port = port_->value();
    result.user = user_->text().trimmed();
    result.password = password_->text();
    result.database = database_->text().trimmed();
    result.sslMode = sslMode_->currentText();
    result.connectTimeoutSeconds = timeout_->value();

    if (address.startsWith(QStringLiteral("postgres://"), Qt::CaseInsensitive)
        || address.startsWith(QStringLiteral("postgresql://"), Qt::CaseInsensitive)) {
        const QUrl url(address);
        if (!url.host().isEmpty())
            result.host = url.host();
        if (url.port() > 0)
            result.port = url.port();
        if (!url.userName().isEmpty())
            result.user = url.userName();
        if (!url.password().isEmpty())
            result.password = url.password();
        const QString path = url.path().mid(1);
        if (!path.isEmpty())
            result.database = path;
        const QString sslMode = QUrlQuery(url).queryItemValue(QStringLiteral("sslmode"));
        if (!sslMode.isEmpty())
            result.sslMode = sslMode;
    }
    return result;
}

void PostgresConnectionDialog::accept()
{
    const PostgresConnectionConfig value = config();
    if (value.host.isEmpty() || value.user.isEmpty() || value.database.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("连接参数不完整"),
                             QStringLiteral("连接地址、用户和数据库不能为空。"));
        return;
    }

    QDialog::accept();
}

} // namespace vsdb

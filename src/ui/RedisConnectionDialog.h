#pragma once

#include "database/redis/RedisSession.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace vsdb {

class RedisConnectionDialog final : public QDialog
{
public:
    explicit RedisConnectionDialog(QWidget *parent = nullptr);

    void setConfig(const RedisConnectionConfig &config);
    RedisConnectionConfig config() const;

protected:
    void accept() override;

private:
    QLineEdit *address_ = nullptr;
    QSpinBox *port_ = nullptr;
    QLineEdit *username_ = nullptr;
    QLineEdit *password_ = nullptr;
    QSpinBox *database_ = nullptr;
    QCheckBox *tls_ = nullptr;
    QSpinBox *timeout_ = nullptr;
};

} // namespace vsdb

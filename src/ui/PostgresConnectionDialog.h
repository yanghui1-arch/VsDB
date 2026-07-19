#pragma once

#include "database/DatabaseTypes.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;

namespace vsdb {

class PostgresConnectionDialog final : public QDialog
{
public:
    explicit PostgresConnectionDialog(QWidget *parent = nullptr);

    void setConfig(const PostgresConnectionConfig &config);
    PostgresConnectionConfig config() const;

protected:
    void accept() override;

private:
    QLineEdit *address_ = nullptr;
    QSpinBox *port_ = nullptr;
    QLineEdit *user_ = nullptr;
    QLineEdit *password_ = nullptr;
    QLineEdit *database_ = nullptr;
    QComboBox *sslMode_ = nullptr;
    QSpinBox *timeout_ = nullptr;
};

} // namespace vsdb

#pragma once

#include <QComboBox>

namespace vsdb {

class ModernComboBox final : public QComboBox
{
public:
    explicit ModernComboBox(QWidget *parent = nullptr);

    void showPopup() override;
};

} // namespace vsdb

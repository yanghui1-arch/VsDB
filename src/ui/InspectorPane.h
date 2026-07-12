#pragma once

#include <QWidget>

class QLabel;
class QTableView;

namespace vsdb {
class InspectorModel;

class InspectorPane final : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPane(InspectorModel *model, QWidget *parent = nullptr);
    void setSelection(const QString &name, const QString &subtitle, bool inspectable);

private:
    QLabel *nameLabel_;
    QLabel *subtitleLabel_;
    QLabel *emptyLabel_;
    QTableView *tableView_;
};

} // namespace vsdb

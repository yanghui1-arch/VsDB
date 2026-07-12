#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTableView;

namespace vsdb {
class ResultTableModel;

class ResultsPane final : public QWidget {
    Q_OBJECT
public:
    explicit ResultsPane(ResultTableModel *model, QWidget *parent = nullptr);
    void beginExecution();
    void finishExecution(int rows, int elapsedMs);
    void appendMessage(const QString &message);
    QTableView *tableView() const;

private:
    QTabWidget *tabs_;
    QTableView *tableView_;
    QLabel *summary_;
    QPlainTextEdit *messages_;
};

} // namespace vsdb

#pragma once

#include <QWidget>

class QPlainTextEdit;
class QSplitter;

namespace vsdb {
class ResultTableModel;
class ResultsPane;

class QueryEditorTab final : public QWidget {
    Q_OBJECT
public:
    explicit QueryEditorTab(ResultTableModel *model, const QString &sql = {}, QWidget *parent = nullptr);
    QString sql() const;
    void clearEditor();
    bool isModified() const;
    void setModified(bool modified);
    ResultsPane *resultsPane() const;
    QByteArray splitterState() const;
    void restoreSplitterState(const QByteArray &state);

signals:
    void modificationChanged(bool modified);

private:
    QSplitter *splitter_;
    QPlainTextEdit *editor_;
    ResultsPane *resultsPane_;
};

} // namespace vsdb

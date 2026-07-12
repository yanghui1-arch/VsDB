#pragma once

#include <QWidget>

class QTabWidget;

namespace vsdb {
class QueryEditorTab;
class ResultTableModel;

class QueryWorkspace final : public QWidget {
    Q_OBJECT
public:
    explicit QueryWorkspace(ResultTableModel *model, QWidget *parent = nullptr);
    QueryEditorTab *currentEditor() const;
    int tabCount() const;
    QByteArray currentSplitterState() const;
    void restoreCurrentSplitterState(const QByteArray &state);

public slots:
    void newQuery(const QString &sql = {});
    void closeCurrentQuery();
    void closeQuery(int index);

private:
    void updateTabTitle(QueryEditorTab *editor, bool modified);
    QTabWidget *tabs_;
    ResultTableModel *model_;
    int nextQueryNumber_ = 1;
};

} // namespace vsdb

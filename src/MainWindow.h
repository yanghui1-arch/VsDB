#pragma once

#include <QMainWindow>

class QComboBox;
class QLabel;
class QSortFilterProxyModel;
class QSplitter;
class QStandardItemModel;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeView;

namespace vsdb {

class QueryPage;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createExplorer();
    QWidget *createWorkspace();
    QWidget *createInspector();
    void createDatabaseToolBar();
    void populateSchema();
    void installActions();
    void updateInspector(const QString &name, const QString &type);
    void addQuery(const QString &sql = {});
    QueryPage *currentQuery() const;

private:
    void executeQuery();
    void stopQuery();
    void refreshSchema();
    void closeQuery(int index);
    void handleSchemaSelection();
    void activateSchemaItem(const QModelIndex &proxyIndex);

private:
    QSplitter *mainSplitter_ = nullptr;
    QTabWidget *queryTabs_ = nullptr;
    QTreeView *schemaTree_ = nullptr;
    QStandardItemModel *schemaModel_ = nullptr;
    QSortFilterProxyModel *schemaProxy_ = nullptr;
    QLabel *objectName_ = nullptr;
    QLabel *objectIcon_ = nullptr;
    QLabel *objectType_ = nullptr;
    QLabel *objectPath_ = nullptr;
    QLabel *rowEstimate_ = nullptr;
    QStandardItemModel *columnModel_ = nullptr;
    QStandardItemModel *indexModel_ = nullptr;
    QStandardItemModel *foreignKeyModel_ = nullptr;
    QTimer *executionTimer_ = nullptr;
    int queryNumber_ = 1;
};

} // namespace vsdb

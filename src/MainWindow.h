#pragma once

#include "database/postgres/PostgresSession.h"

#include <QMainWindow>

class QAction;
class QComboBox;
class QLabel;
class QModelIndex;
class QSortFilterProxyModel;
class QSplitter;
class QStandardItem;
class QStandardItemModel;
class QTabWidget;
class QTreeView;

namespace vsdb {

class QueryPage;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool connectToPostgres(const PostgresConnectionConfig &config, QString *error);
    bool openRelationPreview(const QString &schema, const QString &relation, QString *error);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createExplorer();
    QWidget *createWorkspace();
    QWidget *createInspector();
    void createDatabaseToolBar();
    void populateSchema();
    void loadSchemaChildren(QStandardItem *item);
    void installActions();
    void updateInspector(const QString &name, const QString &type,
                         const QString &schema = {});
    void updateInspector(const DatabaseTable &table);
    void clearInspectorModels();
    void addQuery(const QString &sql = {});
    QueryPage *currentQuery() const;
    void runQuery(QueryPage *page, bool editableTable);
    void updateConnectionUi();
    void showPostgresConnectionDialog();
    void showDatabaseError(const QString &title, const QString &error);

private:
    void executeQuery();
    void stopQuery();
    void refreshSchema();
    void closeQuery(int index);
    void handleSchemaSelection();
    void activateSchemaItem(const QModelIndex &proxyIndex);
    void applyPendingChanges(QueryPage *page);

    PostgresSession postgres_;
    QSplitter *mainSplitter_ = nullptr;
    QTabWidget *queryTabs_ = nullptr;
    QTreeView *schemaTree_ = nullptr;
    QStandardItemModel *schemaModel_ = nullptr;
    QSortFilterProxyModel *schemaProxy_ = nullptr;
    QComboBox *connectionContext_ = nullptr;
    QComboBox *schemaContext_ = nullptr;
    QAction *reconnectAction_ = nullptr;
    QAction *disconnectAction_ = nullptr;
    QLabel *objectName_ = nullptr;
    QLabel *objectIcon_ = nullptr;
    QLabel *objectType_ = nullptr;
    QLabel *objectPath_ = nullptr;
    QLabel *rowEstimate_ = nullptr;
    QLabel *size_ = nullptr;
    QLabel *description_ = nullptr;
    QStandardItemModel *columnModel_ = nullptr;
    QStandardItemModel *indexModel_ = nullptr;
    QStandardItemModel *foreignKeyModel_ = nullptr;
    int queryNumber_ = 1;
};

} // namespace vsdb

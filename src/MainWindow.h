#pragma once

#include "database/ConnectionStore.h"
#include "database/postgres/PostgresSession.h"

#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <optional>

class QAction;
class QComboBox;
class QLabel;
class QModelIndex;
class QPoint;
class QSortFilterProxyModel;
class QSplitter;
class QStandardItem;
class QStandardItemModel;
class QTabWidget;
class QThread;
class QTreeView;

namespace vsdb {

class QueryPage;
class PostgresQueryWorker;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
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
    void populateActiveConnection(QStandardItem *root);
    void populateCachedConnection(QStandardItem *root,
                                  const SavedConnection &connection);
    void updateConnectionSnapshot(QStandardItem *users,
                                  QStandardItem *databases);
    void loadSchemaChildren(QStandardItem *item);
    void installActions();
    void updateInspector(const QString &name, const QString &type,
                         const QString &schema = {});
    void updateInspector(const SavedConnection &connection, bool connected);
    void updateInspector(const DatabaseTable &table);
    void clearInspectorModels();
    void addQuery(const QString &sql = {});
    QueryPage *currentQuery() const;
    void runQuery(QueryPage *page, bool editableTable);
    void initializeQueryWorker();
    void cancelRunningQuery();
    void updateConnectionUi();
    void refreshConnectionPresentation();
    void showPostgresConnectionDialog();
    void editSelectedConnection();
    bool editConnection(
        const QString &connectionId, const QString &notice = {},
        const std::optional<PostgresConnectionConfig> &initialConfig = std::nullopt);
    void connectSelectedConnection();
    void connectSavedConnection(const QString &connectionId,
                                const QString &database = {});
    bool persistActiveConnectionConfig(QString *error);
    void removeSelectedConnection();
    void showConnectionContextMenu(const QPoint &position);
    QString connectionIdForIndex(QModelIndex sourceIndex) const;
    QString selectedConnectionId() const;
    QString preferredConnectionId() const;
    SavedConnection *savedConnection(const QString &connectionId);
    const SavedConnection *savedConnection(const QString &connectionId) const;
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
    std::unique_ptr<CredentialStore> credentialStore_;
    QVector<SavedConnection> savedConnections_;
    QString activeConnectionId_;
    QThread *queryThread_ = nullptr;
    PostgresQueryWorker *queryWorker_ = nullptr;
    QPointer<QueryPage> runningQueryPage_;
    quint64 runningQueryId_ = 0;
    qint64 runningBackendPid_ = -1;
    bool runningQueryEditable_ = false;
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

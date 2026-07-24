#include "MainWindow.h"

#include "SqlEditor.h"
#include "UiComponents.h"
#include "WorkbenchModels.h"
#include "database/postgres/PostgresQueryWorker.h"
#include "database/redis/RedisProtocol.h"
#include "ui/IconProvider.h"
#include "ui/PostgresConnectionDialog.h"
#include "ui/QueryPage.h"
#include "ui/RedisConnectionDialog.h"
#include "ui/RedisKeyPage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QTableView>
#include <QTextDocument>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <utility>

namespace vsdb {

namespace {

constexpr int NameRole = Qt::UserRole;
constexpr int TypeRole = Qt::UserRole + 1;
constexpr int SchemaRole = Qt::UserRole + 2;
constexpr int LoadedRole = Qt::UserRole + 3;
constexpr int ConnectionIdRole = Qt::UserRole + 4;
constexpr int DriverRole = Qt::UserRole + 5;
constexpr int RedisDatabaseRole = Qt::UserRole + 6;
constexpr int RedisCursorRole = Qt::UserRole + 7;
constexpr int RedisKeyRole = Qt::UserRole + 8;
constexpr int RedisLoadedCountRole = Qt::UserRole + 9;
constexpr int OriginalIconRole = Qt::UserRole + 10;
constexpr int MaximumRedisTreeKeys = 2000;

const QString kInitialSql = QStringLiteral(
    "SELECT\n"
    "    current_database() AS database,\n"
    "    current_user AS user_name,\n"
    "    version() AS server_version;");

QToolButton *plainButton(QWidget *parent, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("plainButton"));
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(false);
    return button;
}

QIcon toolbarIcon(const QString &name)
{
    return databaseIcon(name);
}

QStandardItem *schemaItem(const QString &text, const QString &type,
                          const QString &iconName = {}, const QString &name = {},
                          const QString &schema = {}, bool lazy = false)
{
    auto *item = new QStandardItem(text);
    if (!iconName.isEmpty()) {
        const QIcon icon = toolbarIcon(iconName);
        item->setIcon(icon);
        item->setData(icon, OriginalIconRole);
    }
    item->setData(name.isEmpty() ? text : name, NameRole);
    item->setData(type, TypeRole);
    item->setData(schema, SchemaRole);
    item->setData(!lazy, LoadedRole);
    item->setEditable(false);
    if (lazy) {
        auto *loading = new QStandardItem(QStringLiteral("展开以加载…"));
        loading->setData(QStringLiteral("loading"), TypeRole);
        loading->setEditable(false);
        item->appendRow(loading);
    }
    return item;
}

void applyCachedAppearance(QStandardItem *item)
{
    item->setForeground(QColor(QStringLiteral("#686C77")));
    if (!item->icon().isNull()) {
        item->setIcon(QIcon(item->icon().pixmap(
            QSize(16, 16), QIcon::Disabled, QIcon::Off)));
    }
    for (int row = 0; row < item->rowCount(); ++row)
        applyCachedAppearance(item->child(row));
}

void restoreActiveAppearance(QStandardItem *item)
{
    item->setData(QVariant{}, Qt::ForegroundRole);
    const QVariant originalIcon = item->data(OriginalIconRole);
    if (originalIcon.canConvert<QIcon>())
        item->setIcon(qvariant_cast<QIcon>(originalIcon));
    for (int row = 0; row < item->rowCount(); ++row)
        restoreActiveAppearance(item->child(row));
}

void appendReadOnlyRow(QStandardItemModel *model, const QStringList &cells,
                       const QString &icon = {})
{
    QList<QStandardItem *> items;
    for (const QString &cell : cells) {
        auto *item = new QStandardItem(cell);
        item->setEditable(false);
        items.append(item);
    }
    if (!icon.isEmpty() && !items.isEmpty())
        items.constFirst()->setIcon(toolbarIcon(icon));
    model->appendRow(items);
}

QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted"));
    return label;
}

class SchemaTreeView final : public QTreeView
{
public:
    using QTreeView::QTreeView;

protected:
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override
    {
        painter->fillRect(rect, QColor(QStringLiteral("#1B1C23")));
        if (!model() || !model()->hasChildren(index))
            return;
        const QString iconName = isExpanded(index)
            ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right");
        const QRect indicatorSlot(rect.right() - indentation() + 1,
                                  rect.top(), indentation(), rect.height());
        QRect iconRect(0, 0, 14, 14);
        iconRect.moveCenter(indicatorSlot.center());
        toolbarIcon(iconName).paint(painter, iconRect);
    }
};

class AdaptiveQueryTabBar final : public QTabBar
{
public:
    using QTabBar::QTabBar;

    void setAvailableWidth(int width)
    {
        if (availableWidth_ == width)
            return;
        availableWidth_ = width;
        tabLayoutChange();
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QStylePainter painter(this);
        for (int index = 0; index < count(); ++index) {
            QStyleOptionTab option;
            initStyleOption(&option, index);
            painter.drawControl(QStyle::CE_TabBarTabShape, option);

            const QRect tabRect = option.rect;
            int contentLeft = tabRect.left() + 10;
            if (!option.icon.isNull()) {
                const QSize tabIconSize = iconSize();
                const QRect iconRect(contentLeft,
                                     tabRect.center().y() - tabIconSize.height() / 2,
                                     tabIconSize.width(), tabIconSize.height());
                option.icon.paint(&painter, iconRect);
                contentLeft = iconRect.right() + 7;
            }
            int contentRight = tabRect.right() - 8;
            if (const QWidget *closeButton = tabButton(index, QTabBar::RightSide))
                contentRight = closeButton->geometry().left() - 5;

            QFont titleFont = font();
            titleFont.setBold(option.state.testFlag(QStyle::State_Selected));
            painter.setFont(titleFont);
            painter.setPen(option.state.testFlag(QStyle::State_Selected)
                ? QColor(QStringLiteral("#E7E8ED")) : QColor(QStringLiteral("#A3A6B2")));
            painter.drawText(QRect(contentLeft, tabRect.top(),
                                   qMax(0, contentRight - contentLeft + 1), tabRect.height()),
                             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             option.text);
        }
    }

    QSize tabSizeHint(int index) const override
    {
        QSize hint = QTabBar::tabSizeHint(index);
        hint.setWidth(targetTabWidth());
        return hint;
    }

    QSize minimumTabSizeHint(int index) const override
    {
        QSize hint = QTabBar::minimumTabSizeHint(index);
        hint.setWidth(targetTabWidth());
        return hint;
    }

private:
    int targetTabWidth() const
    {
        constexpr int naturalTabWidth = 144;
        constexpr int minimumTabWidth = 44;
        if (count() <= 0 || availableWidth_ <= 0)
            return naturalTabWidth;
        if (count() * naturalTabWidth <= availableWidth_)
            return naturalTabWidth;
        return qMax(minimumTabWidth, availableWidth_ / count());
    }

    int availableWidth_ = 0;
};

class QueryTabWidget final : public QTabWidget
{
public:
    using QTabWidget::QTabWidget;

    void installTabBar(AdaptiveQueryTabBar *bar)
    {
        adaptiveBar_ = bar;
        setTabBar(bar);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTabWidget::resizeEvent(event);
        if (adaptiveBar_)
            adaptiveBar_->setAvailableWidth(qMax(0, event->size().width() - 40));
    }

private:
    AdaptiveQueryTabBar *adaptiveBar_ = nullptr;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), credentialStore_(createPlatformCredentialStore())
{
    setWindowTitle(QStringLiteral("VsDB — 数据库工作台"));
    setMinimumSize(1100, 680);
    resize(1560, 920);
    createDatabaseToolBar();

    auto *rootWidget = new QWidget(this);
    rootWidget->setObjectName(QStringLiteral("appRoot"));
    auto *root = new QVBoxLayout(rootWidget);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    mainSplitter_ = new QSplitter(Qt::Horizontal, rootWidget);
    mainSplitter_->setObjectName(QStringLiteral("workbench"));
    mainSplitter_->setChildrenCollapsible(false);
    mainSplitter_->addWidget(createExplorer());
    mainSplitter_->addWidget(createWorkspace());
    mainSplitter_->addWidget(createInspector());
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 0);
    mainSplitter_->setSizes({300, 930, 330});
    root->addWidget(mainSplitter_, 1);
    setCentralWidget(rootWidget);

    QSettings settings;
    QStringList connectionWarnings;
    savedConnections_ =
        ConnectionStore::load(settings, *credentialStore_, &connectionWarnings);
    savedRedisConnections_ =
        RedisConnectionStore::load(
            settings, *credentialStore_, &connectionWarnings);

    populateSchema();
    installActions();
    initializeQueryWorker();
    addQuery(kInitialSql);
    if (!savedConnections_.isEmpty())
        updateInspector(savedConnections_.constFirst(), false);
    else if (!savedRedisConnections_.isEmpty())
        updateInspector(savedRedisConnections_.constFirst(), false);
    else
        updateInspector(QStringLiteral("Database"), QStringLiteral("connection"));
    updateConnectionUi();

    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    mainSplitter_->restoreState(settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    if (!connectionWarnings.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("已加载连接；%1 个连接需要重新输入密码")
                .arg(connectionWarnings.size()),
            8000);
    }
}

MainWindow::~MainWindow()
{
    if (!queryThread_)
        return;
    if (queryWorker_)
        queryWorker_->requestCancel();
    if (runningBackendPid_ > 0 && postgres_.isConnected()) {
        QString ignoredError;
        postgres_.cancelBackend(runningBackendPid_, &ignoredError);
    }
    queryThread_->quit();
    queryThread_->wait();
    queryWorker_ = nullptr;
}

bool MainWindow::connectToPostgres(const PostgresConnectionConfig &config, QString *error)
{
    cancelRunningQuery();
    if (!postgres_.connectToServer(config, error))
        return false;
    activeConnectionId_.clear();
    focusedDriver_ = QStringLiteral("postgresql");
    refreshConnectionPresentation();
    return true;
}

bool MainWindow::connectToRedis(const RedisConnectionConfig &config, QString *error)
{
    if (!redis_.connectToServer(config, error))
        return false;
    closeRedisPages();
    activeRedisConnectionId_.clear();
    focusedDriver_ = QStringLiteral("redis");
    refreshConnectionPresentation();
    return true;
}

bool MainWindow::openRelationPreview(const QString &schema, const QString &relation,
                                     QString *error)
{
    if (!postgres_.isConnected()) {
        if (error)
            *error = QStringLiteral("PostgreSQL 尚未连接。");
        return false;
    }

    const DatabaseTable table = postgres_.describeTable(schema, relation, error);
    if (error && !error->isEmpty())
        return false;
    addQuery(postgres_.buildRelationPreview(table));
    QueryPage *page = currentQuery();
    const bool editable = !table.primaryKeys().isEmpty();
    if (editable)
        page->setTableContext(postgres_.config().database, schema,
                              relation, table.primaryKeys());
    updateInspector(table);
    runQuery(page, editable);
    return true;
}

void MainWindow::createDatabaseToolBar()
{
    auto *toolbar = new QToolBar(QStringLiteral("Database toolbar"), this);
    toolbar->setObjectName(QStringLiteral("databaseToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::TopToolBarArea, toolbar);

    auto addAction = [toolbar](const QString &icon, const QString &text, const QString &tip) {
        QAction *action = toolbar->addAction(toolbarIcon(icon), text);
        action->setToolTip(tip);
        return action;
    };

    auto *newConnection = new QToolButton(toolbar);
    newConnection->setIcon(toolbarIcon(QStringLiteral("database-zap")));
    newConnection->setToolTip(QStringLiteral("新建数据库连接"));
    newConnection->setPopupMode(QToolButton::MenuButtonPopup);
    auto *connectionMenu = new QMenu(newConnection);
    QAction *postgresAction = connectionMenu->addAction(
        toolbarIcon(QStringLiteral("postgresql")), QStringLiteral("PostgreSQL"));
    connect(postgresAction, &QAction::triggered, this, &MainWindow::showPostgresConnectionDialog);
    QAction *redisAction = connectionMenu->addAction(
        toolbarIcon(QStringLiteral("redis")), QStringLiteral("Redis"));
    connect(redisAction, &QAction::triggered,
            this, &MainWindow::showRedisConnectionDialog);
    connect(newConnection, &QToolButton::clicked, this, &MainWindow::showPostgresConnectionDialog);
    newConnection->setMenu(connectionMenu);
    toolbar->addWidget(newConnection);

    QAction *manage = addAction(QStringLiteral("cloud-cog"), QStringLiteral("连接设置"),
                                QStringLiteral("编辑选中的数据库连接"));
    connect(manage, &QAction::triggered, this, &MainWindow::editSelectedConnection);
    toolbar->addSeparator();

    QAction *connectAction = addAction(QStringLiteral("plug"), QStringLiteral("连接"),
                                       QStringLiteral("连接选中的数据库"));
    connect(connectAction, &QAction::triggered, this, &MainWindow::connectSelectedConnection);
    reconnectAction_ = addAction(QStringLiteral("refresh-cw"), QStringLiteral("重新连接"),
                                 QStringLiteral("重新连接并刷新结构"));
    connect(reconnectAction_, &QAction::triggered, this, [this] {
        const QString driver = selectedConnectionDriver();
        QString error;
        if (driver == QStringLiteral("redis") && redis_.isConnected()) {
            if (!redis_.reconnect(&error)) {
                if (!activeRedisConnectionId_.isEmpty()) {
                    editRedisConnection(
                        activeRedisConnectionId_,
                        QStringLiteral("使用已保存凭据重新连接失败：\n%1\n\n请更新连接信息后重试。")
                            .arg(error));
                } else {
                    showDatabaseError(QStringLiteral("重新连接失败"), error);
                }
                refreshConnectionPresentation();
                return;
            }
            refreshConnectionPresentation();
            statusBar()->showMessage(QStringLiteral("Redis 已重新连接"), 4000);
            return;
        }
        if (driver != QStringLiteral("postgresql")
            || !postgres_.isConnected()) {
            return;
        }
        cancelRunningQuery();
        if (!postgres_.reconnect(&error)) {
            if (!activeConnectionId_.isEmpty()) {
                const bool connected = editConnection(
                    activeConnectionId_,
                    QStringLiteral("使用已保存凭据重新连接失败：\n%1\n\n请更新连接信息后重试。")
                        .arg(error));
                if (!connected)
                    refreshConnectionPresentation();
            } else {
                showDatabaseError(QStringLiteral("重新连接失败"), error);
                refreshConnectionPresentation();
            }
            return;
        }
        refreshConnectionPresentation();
        statusBar()->showMessage(QStringLiteral("PostgreSQL 已重新连接"), 4000);
    });
    disconnectAction_ = addAction(QStringLiteral("unplug"), QStringLiteral("断开连接"),
                                  QStringLiteral("断开当前数据库连接"));
    connect(disconnectAction_, &QAction::triggered, this, [this] {
        const QString driver = selectedConnectionDriver();
        disconnectActiveConnection();
        refreshConnectionPresentation();
        statusBar()->showMessage(
            driver == QStringLiteral("redis")
                ? QStringLiteral("Redis 已断开")
                : QStringLiteral("PostgreSQL 已断开"),
            4000);
    });
    toolbar->addSeparator();

    auto *sqlMenuButton = new QToolButton(toolbar);
    sqlMenuButton->setIcon(toolbarIcon(QStringLiteral("file-code")));
    sqlMenuButton->setText(QStringLiteral("SQL"));
    sqlMenuButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sqlMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto *sqlMenu = new QMenu(sqlMenuButton);
    QAction *runSql = sqlMenu->addAction(QStringLiteral("执行 SQL    Ctrl+Enter"));
    connect(runSql, &QAction::triggered, this, &MainWindow::executeQuery);
    QAction *newSql = sqlMenu->addAction(QStringLiteral("新建 SQL 脚本    Ctrl+T"));
    connect(newSql, &QAction::triggered, this, [this] { addQuery(); });
    sqlMenuButton->setMenu(sqlMenu);
    toolbar->addWidget(sqlMenuButton);
    QAction *newScript = addAction(QStringLiteral("file-plus"), QStringLiteral("新建 SQL 脚本"),
                                   QStringLiteral("新建 SQL 脚本（Ctrl+T）"));
    connect(newScript, &QAction::triggered, this, [this] { addQuery(); });
    toolbar->addSeparator();

    connectionContext_ = new ModernComboBox(toolbar);
    connectionContext_->setObjectName(QStringLiteral("toolbarCombo"));
    connectionContext_->setMinimumWidth(220);
    connectionContext_->setEnabled(false);
    toolbar->addWidget(connectionContext_);

    schemaContext_ = new ModernComboBox(toolbar);
    schemaContext_->setObjectName(QStringLiteral("toolbarCombo"));
    schemaContext_->setMinimumWidth(175);
    schemaContext_->setEnabled(false);
    toolbar->addWidget(schemaContext_);
}

QWidget *MainWindow::createExplorer()
{
    auto *pane = new QWidget(this);
    pane->setObjectName(QStringLiteral("sidePane"));
    pane->setMinimumWidth(225);
    pane->setMaximumWidth(430);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *tabs = new QTabBar(pane);
    tabs->setDrawBase(false);
    tabs->addTab(QStringLiteral("Connections"));
    tabs->setExpanding(false);
    tabs->setFixedHeight(49);
    layout->addWidget(tabs);

    schemaModel_ = new QStandardItemModel(this);
    schemaProxy_ = new QSortFilterProxyModel(this);
    schemaProxy_->setSourceModel(schemaModel_);
    schemaProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    schemaProxy_->setRecursiveFilteringEnabled(true);
    schemaTree_ = new SchemaTreeView(pane);
    schemaTree_->setModel(schemaProxy_);
    schemaTree_->setHeaderHidden(true);
    schemaTree_->setUniformRowHeights(true);
    schemaTree_->setAnimated(false);
    schemaTree_->setIconSize(QSize(16, 16));
    schemaTree_->setIndentation(18);
    schemaTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(schemaTree_, 1);

    connect(schemaTree_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::handleSchemaSelection);
    connect(schemaTree_, &QTreeView::doubleClicked, this, &MainWindow::activateSchemaItem);
    connect(schemaTree_, &QTreeView::customContextMenuRequested,
            this, &MainWindow::showConnectionContextMenu);
    connect(schemaTree_, &QTreeView::expanded, this, [this](const QModelIndex &proxyIndex) {
        const QModelIndex sourceIndex = schemaProxy_->mapToSource(proxyIndex);
        loadSchemaChildren(schemaModel_->itemFromIndex(sourceIndex));
    });
    return pane;
}

QWidget *MainWindow::createWorkspace()
{
    auto *workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("workbench"));
    workspace->setMinimumWidth(0);
    workspace->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto *layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *queryTabWidget = new QueryTabWidget(workspace);
    queryTabWidget->installTabBar(new AdaptiveQueryTabBar(queryTabWidget));
    queryTabs_ = queryTabWidget;
    queryTabs_->setMinimumWidth(0);
    queryTabs_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    queryTabs_->setMovable(true);
    queryTabs_->tabBar()->setObjectName(QStringLiteral("queryTabBar"));
    queryTabs_->tabBar()->setDrawBase(false);
    queryTabs_->tabBar()->setExpanding(false);
    queryTabs_->tabBar()->setIconSize(QSize(14, 14));
    queryTabs_->tabBar()->setFixedHeight(32);
    queryTabs_->tabBar()->setElideMode(Qt::ElideRight);
    queryTabs_->tabBar()->setUsesScrollButtons(false);
    auto *add = plainButton(queryTabs_, QStringLiteral("＋"), QStringLiteral("新建查询（Ctrl+T）"));
    add->setFixedSize(32, 30);
    queryTabs_->setCornerWidget(add, Qt::TopRightCorner);
    connect(add, &QToolButton::clicked, this, [this] { addQuery(); });
    connect(queryTabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeQuery);
    layout->addWidget(queryTabs_);
    return workspace;
}

QWidget *MainWindow::createInspector()
{
    auto *pane = new QWidget(this);
    pane->setObjectName(QStringLiteral("sidePane"));
    pane->setMinimumWidth(285);
    pane->setMaximumWidth(520);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *topTabs = new QTabBar(pane);
    topTabs->setDrawBase(false);
    topTabs->addTab(QStringLiteral("Inspector"));
    topTabs->addTab(QStringLiteral("Query History"));
    topTabs->setExpanding(false);
    topTabs->setFixedHeight(49);
    layout->addWidget(topTabs);

    auto *body = new QWidget(pane);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(16, 14, 16, 0);
    bodyLayout->setSpacing(12);
    auto *heading = new QWidget(body);
    auto *headingLayout = new QHBoxLayout(heading);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    objectIcon_ = new QLabel(heading);
    objectIcon_->setObjectName(QStringLiteral("objectIcon"));
    objectIcon_->setAlignment(Qt::AlignCenter);
    objectIcon_->setFixedSize(46, 46);
    headingLayout->addWidget(objectIcon_);
    auto *names = new QWidget(heading);
    auto *namesLayout = new QVBoxLayout(names);
    namesLayout->setContentsMargins(0, 0, 0, 0);
    namesLayout->setSpacing(1);
    objectName_ = new QLabel(names);
    objectName_->setObjectName(QStringLiteral("paneHeading"));
    objectType_ = mutedLabel({}, names);
    namesLayout->addWidget(objectName_);
    namesLayout->addWidget(objectType_);
    headingLayout->addWidget(names, 1);
    bodyLayout->addWidget(heading);
    objectPath_ = mutedLabel({}, body);
    objectPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bodyLayout->addWidget(objectPath_);

    auto *details = new QWidget(body);
    auto *form = new QFormLayout(details);
    form->setContentsMargins(0, 3, 0, 3);
    form->setHorizontalSpacing(28);
    form->setVerticalSpacing(9);
    rowEstimate_ = new QLabel(QStringLiteral("—"), details);
    size_ = new QLabel(QStringLiteral("—"), details);
    description_ = new QLabel(QStringLiteral("—"), details);
    description_->setWordWrap(true);
    form->addRow(mutedLabel(QStringLiteral("Row estimate"), details), rowEstimate_);
    form->addRow(mutedLabel(QStringLiteral("Size"), details), size_);
    form->addRow(mutedLabel(QStringLiteral("Description"), details), description_);
    bodyLayout->addWidget(details);

    auto *metadataTabs = new QTabWidget(body);
    metadataTabs->tabBar()->setObjectName(QStringLiteral("metadataTabBar"));
    metadataTabs->tabBar()->setDrawBase(false);
    metadataTabs->tabBar()->setExpanding(true);
    metadataTabs->tabBar()->setUsesScrollButtons(false);
    metadataTabs->tabBar()->setElideMode(Qt::ElideRight);
    columnModel_ = new QStandardItemModel(0, 4, metadataTabs);
    columnModel_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                             QStringLiteral("Nullable"), QStringLiteral("Default")});
    auto *columns = new QTableView(metadataTabs);
    columns->setModel(columnModel_);
    columns->verticalHeader()->hide();
    columns->setShowGrid(false);
    columns->setSelectionBehavior(QAbstractItemView::SelectRows);
    columns->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    columns->setColumnWidth(1, 90);
    columns->setColumnWidth(2, 70);
    metadataTabs->addTab(columns, QStringLiteral("Columns"));

    indexModel_ = new QStandardItemModel(0, 2, metadataTabs);
    indexModel_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Definition")});
    auto *indexes = new QTableView(metadataTabs);
    indexes->setModel(indexModel_);
    indexes->verticalHeader()->hide();
    indexes->setShowGrid(false);
    indexes->setSelectionBehavior(QAbstractItemView::SelectRows);
    indexes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    metadataTabs->addTab(indexes, QStringLiteral("Indexes"));

    foreignKeyModel_ = new QStandardItemModel(0, 2, metadataTabs);
    foreignKeyModel_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Definition")});
    auto *foreignKeys = new QTableView(metadataTabs);
    foreignKeys->setModel(foreignKeyModel_);
    foreignKeys->verticalHeader()->hide();
    foreignKeys->setShowGrid(false);
    foreignKeys->setSelectionBehavior(QAbstractItemView::SelectRows);
    foreignKeys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    metadataTabs->addTab(foreignKeys, QStringLiteral("Foreign Keys"));
    bodyLayout->addWidget(metadataTabs, 2);

    auto *pages = new QStackedWidget(pane);
    pages->addWidget(body);
    auto *historyPage = new QWidget(pages);
    historyPage->setObjectName(QStringLiteral("sidePane"));
    auto *historyLayout = new QVBoxLayout(historyPage);
    historyLayout->addWidget(mutedLabel(QStringLiteral("查询执行结果会显示在各查询页的 Messages 中。"), historyPage));
    historyLayout->addStretch();
    pages->addWidget(historyPage);
    layout->addWidget(pages, 1);
    connect(topTabs, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);
    return pane;
}

void MainWindow::populateSchema()
{
    schemaModel_->clear();
    QStandardItem *selectedRoot = nullptr;
    QStandardItem *activeRoot = nullptr;
    QStandardItem *activeRedisRoot = nullptr;

    for (const SavedConnection &connection : savedConnections_) {
        const bool active = postgres_.isConnected()
            && connection.id == activeConnectionId_;
        const PostgresConnectionConfig &displayConfig =
            active ? postgres_.config() : connection.config;
        const QString text = active
            ? QStringLiteral("PostgreSQL · %1").arg(displayConfig.displayName())
            : QStringLiteral("PostgreSQL · %1（未连接）")
                  .arg(displayConfig.displayName());
        auto *root = schemaItem(text, QStringLiteral("connection"),
                                QStringLiteral("postgresql"),
                                displayConfig.displayName());
        root->setData(connection.id, ConnectionIdRole);
        root->setData(QStringLiteral("postgresql"), DriverRole);
        root->setToolTip(active
            ? QStringLiteral("当前已连接")
            : QStringLiteral("双击后使用已保存的账号和密码自动连接"));
        schemaModel_->appendRow(root);
        if (!selectedRoot)
            selectedRoot = root;
        if (connection.id == activeConnectionId_)
            selectedRoot = root;
        if (!active) {
            populateCachedConnection(root, connection);
            continue;
        }

        activeRoot = root;
        selectedRoot = root;
        populateActiveConnection(root);
    }

    for (const SavedRedisConnection &connection : savedRedisConnections_) {
        const bool active = redis_.isConnected()
            && connection.id == activeRedisConnectionId_;
        const RedisConnectionConfig &displayConfig =
            active ? redis_.config() : connection.config;
        const QString text = active
            ? QStringLiteral("Redis · %1").arg(displayConfig.displayName())
            : QStringLiteral("Redis · %1（未连接）")
                  .arg(displayConfig.displayName());
        auto *root = schemaItem(text, QStringLiteral("redis-connection"),
                                QStringLiteral("redis"),
                                displayConfig.displayName());
        root->setData(connection.id, ConnectionIdRole);
        root->setData(QStringLiteral("redis"), DriverRole);
        root->setToolTip(
            active ? QStringLiteral("当前已连接")
                   : QStringLiteral("双击后使用已保存的凭据自动连接"));
        schemaModel_->appendRow(root);
        if (!selectedRoot)
            selectedRoot = root;
        if (connection.id == activeRedisConnectionId_)
            selectedRoot = root;
        if (active) {
            activeRedisRoot = root;
            selectedRoot = root;
            populateActiveRedisConnection(root);
        } else {
            auto *databases = schemaItem(
                QStringLiteral("Databases"),
                QStringLiteral("redis-databases"),
                QStringLiteral("database"));
            root->appendRow(databases);
            if (connection.databaseSnapshot.isEmpty()) {
                auto *database = schemaItem(
                    QStringLiteral("db%1").arg(displayConfig.database),
                    QStringLiteral("redis-database"),
                    QStringLiteral("database"),
                    QStringLiteral("db%1").arg(displayConfig.database));
                database->setData(
                    displayConfig.database, RedisDatabaseRole);
                databases->appendRow(database);
            } else {
                for (const RedisDatabaseInfo &databaseInfo :
                     connection.databaseSnapshot) {
                    auto *database = schemaItem(
                        QStringLiteral("db%1 · %2 keys")
                            .arg(databaseInfo.index)
                            .arg(databaseInfo.keys),
                        QStringLiteral("redis-database"),
                        QStringLiteral("database"),
                        QStringLiteral("db%1")
                            .arg(databaseInfo.index));
                    database->setData(
                        databaseInfo.index, RedisDatabaseRole);
                    database->setToolTip(
                        QStringLiteral("上次成功连接时的数据库快照"));
                    databases->appendRow(database);
                }
            }
            applyCachedAppearance(databases);
            schemaTree_->expand(schemaProxy_->mapFromSource(root->index()));
            schemaTree_->expand(
                schemaProxy_->mapFromSource(databases->index()));
        }
    }

    if (postgres_.isConnected() && !activeRoot) {
        auto *root = schemaItem(
            QStringLiteral("PostgreSQL · %1（本次会话）")
                .arg(postgres_.config().displayName()),
            QStringLiteral("connection"), QStringLiteral("postgresql"),
            postgres_.config().displayName());
        root->setData(QStringLiteral("postgresql"), DriverRole);
        schemaModel_->appendRow(root);
        activeRoot = root;
        selectedRoot = root;
        populateActiveConnection(root);
    }
    if (redis_.isConnected() && !activeRedisRoot) {
        auto *root = schemaItem(
            QStringLiteral("Redis · %1（本次会话）")
                .arg(redis_.config().displayName()),
            QStringLiteral("redis-connection"), QStringLiteral("redis"),
            redis_.config().displayName());
        root->setData(QStringLiteral("redis"), DriverRole);
        schemaModel_->appendRow(root);
        activeRedisRoot = root;
        selectedRoot = root;
        populateActiveRedisConnection(root);
    }
    if (!selectedRoot) {
        selectedRoot = schemaItem(
            QStringLiteral("尚无已保存连接，点击左上角数据库图标添加"),
            QStringLiteral("empty"), QStringLiteral("database"),
            QStringLiteral("Database"));
        schemaModel_->appendRow(selectedRoot);
    }

    if (focusedDriver_ == QStringLiteral("postgresql") && activeRoot)
        selectedRoot = activeRoot;
    else if (focusedDriver_ == QStringLiteral("redis") && activeRedisRoot)
        selectedRoot = activeRedisRoot;
    else if (activeRoot && !activeRedisRoot)
        selectedRoot = activeRoot;
    else if (activeRedisRoot && !activeRoot)
        selectedRoot = activeRedisRoot;

    if (postgres_.isConnected() || redis_.isConnected()) {
        schemaTree_->setCurrentIndex(
            schemaProxy_->mapFromSource(selectedRoot->index()));
    } else {
        schemaTree_->setCurrentIndex(QModelIndex{});
    }
}

void MainWindow::populateActiveRedisConnection(QStandardItem *root)
{
    auto *databases = schemaItem(
        QStringLiteral("Databases"), QStringLiteral("redis-databases"),
        QStringLiteral("database"), {}, {}, true);
    root->appendRow(databases);
    loadSchemaChildren(databases);
    schemaTree_->expand(schemaProxy_->mapFromSource(root->index()));
    schemaTree_->expand(schemaProxy_->mapFromSource(databases->index()));
    for (int row = 0; row < databases->rowCount(); ++row) {
        QStandardItem *database = databases->child(row);
        if (database->data(RedisDatabaseRole).toInt()
            != redis_.config().database) {
            continue;
        }
        schemaTree_->expand(schemaProxy_->mapFromSource(database->index()));
        if (database->rowCount() > 0) {
            QStandardItem *keys = database->child(0);
            loadSchemaChildren(keys);
            schemaTree_->expand(schemaProxy_->mapFromSource(keys->index()));
        }
        break;
    }
}

bool MainWindow::selectPostgresDatabaseInTree(const QString &database)
{
    for (int rootRow = 0; rootRow < schemaModel_->rowCount(); ++rootRow) {
        QStandardItem *root = schemaModel_->item(rootRow);
        if (!root
            || root->data(DriverRole).toString()
                != QStringLiteral("postgresql")) {
            continue;
        }
        const QString connectionId =
            root->data(ConnectionIdRole).toString();
        if ((!activeConnectionId_.isEmpty()
             && connectionId != activeConnectionId_)
            || (activeConnectionId_.isEmpty()
                && !connectionId.isEmpty())) {
            continue;
        }
        root->setText(
            QStringLiteral("PostgreSQL · %1")
                .arg(postgres_.config().displayName()));
        root->setData(postgres_.config().displayName(), NameRole);
        for (int childRow = 0; childRow < root->rowCount(); ++childRow) {
            QStandardItem *databases = root->child(childRow);
            if (!databases
                || databases->data(TypeRole).toString()
                    != QStringLiteral("databases")) {
                continue;
            }
            for (int databaseRow = 0;
                 databaseRow < databases->rowCount(); ++databaseRow) {
                QStandardItem *databaseItem =
                    databases->child(databaseRow);
                if (!databaseItem
                    || databaseItem->data(NameRole).toString()
                        != database) {
                    continue;
                }
                const QModelIndex rootIndex =
                    schemaProxy_->mapFromSource(root->index());
                const QModelIndex databasesIndex =
                    schemaProxy_->mapFromSource(databases->index());
                const QModelIndex databaseIndex =
                    schemaProxy_->mapFromSource(databaseItem->index());
                schemaTree_->expand(rootIndex);
                schemaTree_->expand(databasesIndex);
                schemaTree_->expand(databaseIndex);
                schemaTree_->setCurrentIndex(databaseIndex);
                schemaTree_->scrollTo(
                    databaseIndex, QAbstractItemView::PositionAtCenter);
                return true;
            }
        }
    }
    return false;
}

QString MainWindow::postgresDatabaseForIndex(
    const QModelIndex &sourceIndex) const
{
    QModelIndex current = sourceIndex;
    while (current.isValid()) {
        if (current.data(TypeRole).toString()
            == QStringLiteral("database")) {
            return current.data(NameRole).toString();
        }
        current = current.parent();
    }
    return {};
}

bool MainWindow::activatePostgresDatabase(const QString &database,
                                          QString *error)
{
    if (database.isEmpty()
        || database == postgres_.config().database) {
        return true;
    }
    if (!postgres_.switchDatabase(database, error))
        return false;

    QString saveError;
    if (!activeConnectionId_.isEmpty()
        && !persistActiveConnectionConfig(&saveError)) {
        statusBar()->showMessage(
            QStringLiteral("数据库已切换，但无法保存默认数据库：%1")
                .arg(saveError),
            5000);
    }
    updatePostgresDatabaseAppearance();
    updateConnectionUi();
    return true;
}

void MainWindow::updatePostgresDatabaseAppearance()
{
    for (int rootRow = 0; rootRow < schemaModel_->rowCount(); ++rootRow) {
        QStandardItem *root = schemaModel_->item(rootRow);
        if (!root
            || root->data(DriverRole).toString()
                != QStringLiteral("postgresql")) {
            continue;
        }
        const QString connectionId =
            root->data(ConnectionIdRole).toString();
        if ((!activeConnectionId_.isEmpty()
             && connectionId != activeConnectionId_)
            || (activeConnectionId_.isEmpty()
                && !connectionId.isEmpty())) {
            continue;
        }
        for (int childRow = 0; childRow < root->rowCount(); ++childRow) {
            QStandardItem *databases = root->child(childRow);
            if (!databases
                || databases->data(TypeRole).toString()
                    != QStringLiteral("databases")) {
                continue;
            }
            for (int databaseRow = 0;
                 databaseRow < databases->rowCount(); ++databaseRow) {
                QStandardItem *databaseItem =
                    databases->child(databaseRow);
                const QString database =
                    databaseItem->data(NameRole).toString();
                const bool connected =
                    postgres_.isDatabaseConnected(database);
                const bool current =
                    database == postgres_.config().database;
                if (connected && databaseItem->rowCount() == 0) {
                    databaseItem->appendRow(schemaItem(
                        QStringLiteral("Schemas"),
                        QStringLiteral("schemas"),
                        QStringLiteral("boxes"), {}, {}, true));
                }
                QFont font = databaseItem->font();
                font.setBold(current);
                databaseItem->setFont(font);
                if (current) {
                    restoreActiveAppearance(databaseItem);
                    databaseItem->setToolTip(
                        QStringLiteral("当前 PostgreSQL 数据库"));
                } else if (connected) {
                    restoreActiveAppearance(databaseItem);
                    databaseItem->setToolTip(
                        QStringLiteral("会话已保持连接，双击可切换"));
                } else {
                    applyCachedAppearance(databaseItem);
                    databaseItem->setToolTip(
                        QStringLiteral("双击可建立数据库连接"));
                }
            }
        }
    }
}

bool MainWindow::selectRedisDatabaseInTree(int database)
{
    for (int rootRow = 0; rootRow < schemaModel_->rowCount(); ++rootRow) {
        QStandardItem *root = schemaModel_->item(rootRow);
        if (!root
            || root->data(DriverRole).toString() != QStringLiteral("redis")) {
            continue;
        }
        for (int childRow = 0; childRow < root->rowCount(); ++childRow) {
            QStandardItem *databases = root->child(childRow);
            if (!databases
                || databases->data(TypeRole).toString()
                    != QStringLiteral("redis-databases")) {
                continue;
            }
            for (int databaseRow = 0;
                 databaseRow < databases->rowCount(); ++databaseRow) {
                QStandardItem *databaseItem =
                    databases->child(databaseRow);
                if (!databaseItem
                    || databaseItem->data(RedisDatabaseRole).toInt()
                        != database) {
                    continue;
                }
                const QModelIndex rootIndex =
                    schemaProxy_->mapFromSource(root->index());
                const QModelIndex databasesIndex =
                    schemaProxy_->mapFromSource(databases->index());
                const QModelIndex databaseIndex =
                    schemaProxy_->mapFromSource(databaseItem->index());
                schemaTree_->expand(rootIndex);
                schemaTree_->expand(databasesIndex);
                schemaTree_->expand(databaseIndex);
                schemaTree_->setCurrentIndex(databaseIndex);
                schemaTree_->scrollTo(
                    databaseIndex, QAbstractItemView::PositionAtCenter);
                return true;
            }
        }
    }
    return false;
}

void MainWindow::populateActiveConnection(QStandardItem *root)
{
    auto *users = schemaItem(QStringLiteral("Users"), QStringLiteral("users"),
                             QStringLiteral("key-round"), {}, {}, true);
    auto *databases = schemaItem(QStringLiteral("Databases"), QStringLiteral("databases"),
                                 QStringLiteral("database"), {}, {}, true);
    root->appendRow(users);
    root->appendRow(databases);
    loadSchemaChildren(users);
    loadSchemaChildren(databases);
    schemaTree_->expand(schemaProxy_->mapFromSource(root->index()));
    schemaTree_->expand(schemaProxy_->mapFromSource(users->index()));
    schemaTree_->expand(schemaProxy_->mapFromSource(databases->index()));

    for (int row = 0; row < databases->rowCount(); ++row) {
        QStandardItem *database = databases->child(row);
        if (database->data(NameRole).toString() != postgres_.config().database)
            continue;
        schemaTree_->expand(schemaProxy_->mapFromSource(database->index()));
        QStandardItem *schemas = database->child(0);
        if (!schemas)
            break;
        loadSchemaChildren(schemas);
        schemaTree_->expand(schemaProxy_->mapFromSource(schemas->index()));
        for (int schemaRow = 0; schemaRow < schemas->rowCount(); ++schemaRow) {
            QStandardItem *schema = schemas->child(schemaRow);
            if (schema->data(NameRole).toString() != QStringLiteral("public"))
                continue;
            loadSchemaChildren(schema);
            schemaTree_->expand(schemaProxy_->mapFromSource(schema->index()));
            for (int relationGroup = 0; relationGroup < schema->rowCount();
                 ++relationGroup) {
                schemaTree_->expand(schemaProxy_->mapFromSource(
                    schema->child(relationGroup)->index()));
            }
            break;
        }
        break;
    }
    updateConnectionSnapshot(users, databases);
}

void MainWindow::populateCachedConnection(
    QStandardItem *root, const SavedConnection &connection)
{
    QStringList users = connection.snapshot.users;
    if (!users.contains(connection.config.user))
        users.prepend(connection.config.user);
    QStringList databases = connection.snapshot.databases;
    if (!databases.contains(connection.config.database))
        databases.prepend(connection.config.database);

    auto *usersItem = schemaItem(QStringLiteral("Users"), QStringLiteral("users"),
                                 QStringLiteral("key-round"));
    for (const QString &user : users) {
        usersItem->appendRow(schemaItem(user, QStringLiteral("user"),
                                        QStringLiteral("key-round"), user));
    }
    root->appendRow(usersItem);

    auto *databasesItem = schemaItem(QStringLiteral("Databases"),
                                     QStringLiteral("databases"),
                                     QStringLiteral("database"));
    root->appendRow(databasesItem);
    for (const QString &databaseName : databases) {
        auto *database = schemaItem(databaseName, QStringLiteral("database"),
                                    QStringLiteral("database"), databaseName);
        databasesItem->appendRow(database);
        if (databaseName != connection.config.database)
            continue;

        auto *schemas = schemaItem(QStringLiteral("Schemas"),
                                   QStringLiteral("schemas"),
                                   QStringLiteral("boxes"));
        database->appendRow(schemas);
        for (const QString &schemaName : connection.snapshot.schemas) {
            auto *schema = schemaItem(schemaName, QStringLiteral("schema"),
                                      QStringLiteral("boxes"), schemaName,
                                      schemaName);
            schemas->appendRow(schema);
            if (schemaName != QStringLiteral("public"))
                continue;

            auto *tables = schemaItem(QStringLiteral("Tables"),
                                      QStringLiteral("tables"),
                                      QStringLiteral("table-2"));
            for (const QString &table : connection.snapshot.publicTables) {
                tables->appendRow(schemaItem(
                    table, QStringLiteral("Table"), QStringLiteral("table-2"),
                    table, QStringLiteral("public")));
            }
            schema->appendRow(tables);

            auto *views = schemaItem(QStringLiteral("Views"),
                                     QStringLiteral("views"),
                                     QStringLiteral("eye"));
            for (const QString &view : connection.snapshot.publicViews) {
                views->appendRow(schemaItem(
                    view, QStringLiteral("View"), QStringLiteral("eye"),
                    view, QStringLiteral("public")));
            }
            schema->appendRow(views);
        }
    }

    applyCachedAppearance(usersItem);
    applyCachedAppearance(databasesItem);
    schemaTree_->expand(schemaProxy_->mapFromSource(root->index()));
    schemaTree_->expand(schemaProxy_->mapFromSource(usersItem->index()));
    schemaTree_->expand(schemaProxy_->mapFromSource(databasesItem->index()));
}

void MainWindow::updateConnectionSnapshot(
    QStandardItem *users, QStandardItem *databases)
{
    SavedConnection *connection = savedConnection(activeConnectionId_);
    // Session-only connections must not overwrite a saved profile's cached tree.
    if (!connection
        || connection->config.database != postgres_.config().database) {
        return;
    }

    ConnectionSnapshot snapshot;
    for (int row = 0; row < users->rowCount(); ++row)
        snapshot.users.append(users->child(row)->data(NameRole).toString());
    for (int row = 0; row < databases->rowCount(); ++row) {
        QStandardItem *database = databases->child(row);
        snapshot.databases.append(database->data(NameRole).toString());
        if (database->data(NameRole).toString() != connection->config.database)
            continue;
        QStandardItem *schemas = database->child(0);
        if (!schemas)
            continue;
        for (int schemaRow = 0; schemaRow < schemas->rowCount(); ++schemaRow) {
            QStandardItem *schema = schemas->child(schemaRow);
            const QString schemaName = schema->data(NameRole).toString();
            snapshot.schemas.append(schemaName);
            if (schemaName != QStringLiteral("public"))
                continue;
            for (int groupRow = 0; groupRow < schema->rowCount(); ++groupRow) {
                QStandardItem *group = schema->child(groupRow);
                QStringList *relations = nullptr;
                const QString groupType = group->data(TypeRole).toString();
                if (groupType == QStringLiteral("tables"))
                    relations = &snapshot.publicTables;
                else if (groupType == QStringLiteral("views"))
                    relations = &snapshot.publicViews;
                else
                    continue;
                for (int relationRow = 0; relationRow < group->rowCount();
                     ++relationRow) {
                    relations->append(
                        group->child(relationRow)->data(NameRole).toString());
                }
            }
        }
    }

    connection->snapshot = std::move(snapshot);
    QSettings settings;
    QString error;
    if (!ConnectionStore::saveSnapshot(settings, *connection, &error))
        statusBar()->showMessage(error, 5000);
}

void MainWindow::loadSchemaChildren(QStandardItem *item)
{
    if (!item || item->data(LoadedRole).toBool())
        return;

    const QString type = item->data(TypeRole).toString();
    if (type.startsWith(QStringLiteral("redis-"))) {
        if (!redis_.isConnected())
            return;
        QString error;
        item->setData(true, LoadedRole);
        item->removeRows(0, item->rowCount());

        if (type == QStringLiteral("redis-databases")) {
            const QVector<RedisDatabaseInfo> databases = redis_.databases(&error);
            if (!activeRedisConnectionId_.isEmpty()) {
                if (SavedRedisConnection *connection =
                        savedRedisConnection(
                            activeRedisConnectionId_)) {
                    connection->databaseSnapshot = databases;
                    QSettings settings;
                    QString snapshotError;
                    if (!RedisConnectionStore::saveSnapshot(
                            settings, *connection, &snapshotError)) {
                        statusBar()->showMessage(
                            snapshotError, 5000);
                    }
                }
            }
            for (const RedisDatabaseInfo &databaseInfo : databases) {
                const bool current =
                    databaseInfo.index == redis_.config().database;
                QString text =
                    QStringLiteral("db%1 · %2 keys")
                        .arg(databaseInfo.index)
                        .arg(databaseInfo.keys);
                auto *database = schemaItem(
                    text, QStringLiteral("redis-database"),
                    QStringLiteral("database"),
                    QStringLiteral("db%1").arg(databaseInfo.index));
                database->setData(databaseInfo.index, RedisDatabaseRole);
                if (current) {
                    QFont font = database->font();
                    font.setBold(true);
                    database->setFont(font);
                }
                if (current) {
                    auto *keys = schemaItem(
                        QStringLiteral("Keys"), QStringLiteral("redis-keys"),
                        QStringLiteral("key-round"), {}, {}, true);
                    keys->setData(databaseInfo.index, RedisDatabaseRole);
                    database->appendRow(keys);
                } else {
                    applyCachedAppearance(database);
                    database->setToolTip(
                        QStringLiteral("双击可切换到此 Redis 数据库"));
                }
                item->appendRow(database);
            }
        } else if (type == QStringLiteral("redis-keys")) {
            const int database = item->data(RedisDatabaseRole).toInt();
            if (database != redis_.config().database
                && !redis_.selectDatabase(database, &error)) {
                item->setData(false, LoadedRole);
            } else {
                if (!activeRedisConnectionId_.isEmpty()) {
                    QString ignoredError;
                    persistActiveRedisConnectionConfig(&ignoredError);
                }
                loadRedisKeys(item, QByteArrayLiteral("0"), false);
                return;
            }
        }

        if (!error.isEmpty()) {
            item->setData(false, LoadedRole);
            item->appendRow(schemaItem(
                QStringLiteral("加载失败，展开重试"),
                QStringLiteral("loading")));
            showDatabaseError(QStringLiteral("加载 Redis 结构失败"), error);
        }
        return;
    }

    if (!postgres_.isConnected())
        return;

    QString error;
    const QString targetDatabase =
        postgresDatabaseForIndex(item->index());
    if (!targetDatabase.isEmpty()
        && targetDatabase != postgres_.config().database
        && !activatePostgresDatabase(targetDatabase, &error)) {
        showDatabaseError(
            QStringLiteral("切换 PostgreSQL 数据库失败"), error);
        return;
    }
    item->setData(true, LoadedRole);
    item->removeRows(0, item->rowCount());

    if (type == QStringLiteral("users")) {
        const QStringList users = postgres_.users(&error);
        for (const QString &user : users) {
            const bool current = user == postgres_.currentUser();
            QStandardItem *child = schemaItem(
                user, QStringLiteral("user"),
                QStringLiteral("key-round"), user);
            if (current) {
                QFont font = child->font();
                font.setBold(true);
                child->setFont(font);
            }
            item->appendRow(child);
        }
    } else if (type == QStringLiteral("databases")) {
        const QStringList databases = postgres_.databases(&error);
        for (const QString &databaseName : databases) {
            const bool current = databaseName == postgres_.config().database;
            const bool connected =
                postgres_.isDatabaseConnected(databaseName);
            QStandardItem *database = schemaItem(
                databaseName,
                QStringLiteral("database"), QStringLiteral("database"), databaseName);
            if (connected) {
                database->appendRow(schemaItem(
                    QStringLiteral("Schemas"),
                    QStringLiteral("schemas"),
                    QStringLiteral("boxes"), {}, {}, true));
            }
            if (current) {
                QFont font = database->font();
                font.setBold(true);
                database->setFont(font);
                database->setToolTip(
                    QStringLiteral("当前 PostgreSQL 数据库"));
            } else if (connected) {
                database->setToolTip(
                    QStringLiteral("会话已保持连接，双击可切换"));
            } else {
                applyCachedAppearance(database);
                database->setToolTip(
                    QStringLiteral("双击可建立数据库连接"));
            }
            item->appendRow(database);
        }
    } else if (type == QStringLiteral("schemas")) {
        const QStringList schemas = postgres_.schemas(&error);
        for (const QString &schema : schemas)
            item->appendRow(schemaItem(schema, QStringLiteral("schema"), QStringLiteral("boxes"),
                                       schema, schema, true));
    } else if (type == QStringLiteral("schema")) {
        const QString schema = item->data(NameRole).toString();
        const QVector<DatabaseRelation> relations = postgres_.relations(schema, &error);
        auto *tables = schemaItem(QStringLiteral("Tables"), QStringLiteral("tables"),
                                  QStringLiteral("table-2"));
        auto *views = schemaItem(QStringLiteral("Views"), QStringLiteral("views"),
                                 QStringLiteral("eye"));
        for (const DatabaseRelation &relation : relations) {
            QStandardItem *relationItem = schemaItem(
                relation.name, relation.view ? QStringLiteral("View") : QStringLiteral("Table"),
                relation.view ? QStringLiteral("eye") : QStringLiteral("table-2"),
                relation.name, relation.schema);
            (relation.view ? views : tables)->appendRow(relationItem);
        }
        item->appendRow(tables);
        item->appendRow(views);
    }

    if (!error.isEmpty()) {
        item->setData(false, LoadedRole);
        item->appendRow(schemaItem(QStringLiteral("加载失败，展开重试"), QStringLiteral("loading")));
        showDatabaseError(QStringLiteral("加载数据库结构失败"), error);
    }
}

void MainWindow::loadRedisKeys(QStandardItem *item,
                               const QByteArray &cursor, bool append)
{
    if (!item || !redis_.isConnected())
        return;

    if (!append) {
        item->removeRows(0, item->rowCount());
        item->setData(0, RedisLoadedCountRole);
    } else if (item->rowCount() > 0
               && item->child(item->rowCount() - 1)
                      ->data(TypeRole).toString()
                   == QStringLiteral("redis-load-more")) {
        item->removeRow(item->rowCount() - 1);
    }

    QString error;
    const RedisScanPage page =
        redis_.scanKeys(cursor, redisKeyPattern_, 200, &error);
    if (!error.isEmpty()) {
        showDatabaseError(QStringLiteral("加载 Redis 键失败"), error);
        return;
    }

    int loaded = item->data(RedisLoadedCountRole).toInt();
    for (const QByteArray &key : page.keys) {
        if (loaded >= MaximumRedisTreeKeys)
            break;
        auto *keyItem = schemaItem(
            redisDisplayBytes(key, 160), QStringLiteral("redis-key"),
            QStringLiteral("key-round"));
        keyItem->setData(key, RedisKeyRole);
        keyItem->setToolTip(redisDisplayBytes(key, 4096));
        item->appendRow(keyItem);
        ++loaded;
    }
    item->setData(loaded, RedisLoadedCountRole);
    item->setText(
        redisKeyPattern_ == QByteArrayLiteral("*")
            ? QStringLiteral("Keys · %1").arg(loaded)
            : QStringLiteral("Keys · %1 · %2")
                  .arg(redisDisplayBytes(redisKeyPattern_, 48))
                  .arg(loaded));

    if (page.nextCursor != QByteArrayLiteral("0")
        && loaded < MaximumRedisTreeKeys) {
        auto *more = schemaItem(
            QStringLiteral("继续加载…"), QStringLiteral("redis-load-more"),
            QStringLiteral("refresh-cw"));
        more->setData(page.nextCursor, RedisCursorRole);
        item->appendRow(more);
    } else if (page.nextCursor != QByteArrayLiteral("0")) {
        item->appendRow(schemaItem(
            QStringLiteral("已达到 %1 个键的可视化上限，请缩小数据范围")
                .arg(MaximumRedisTreeKeys),
            QStringLiteral("redis-limit"), QStringLiteral("key-round")));
    } else if (loaded == 0) {
        item->appendRow(schemaItem(
            QStringLiteral("此数据库暂无键"),
            QStringLiteral("redis-empty"), QStringLiteral("key-round")));
    }
}

void MainWindow::installActions()
{
    auto *run = new QAction(this);
    run->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    connect(run, &QAction::triggered, this, &MainWindow::executeQuery);
    addAction(run);
    auto *newQueryAction = new QAction(this);
    newQueryAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(newQueryAction, &QAction::triggered, this, [this] { addQuery(); });
    addAction(newQueryAction);
    auto *close = new QAction(this);
    close->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(close, &QAction::triggered, this, [this] { closeQuery(queryTabs_->currentIndex()); });
    addAction(close);
    auto *stop = new QAction(this);
    stop->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(stop, &QAction::triggered, this, &MainWindow::stopQuery);
    addAction(stop);
}

void MainWindow::addQuery(const QString &sql)
{
    auto *page = new QueryPage(sql.isEmpty() ? QStringLiteral("SELECT version();") : sql, queryTabs_);
    if (postgres_.isConnected())
        page->setDatabaseContext(postgres_.config().database);
    const QString title = QStringLiteral("Query %1").arg(queryNumber_++);
    page->setProperty("tabTitle", title);
    const int index = queryTabs_->addTab(page, toolbarIcon(QStringLiteral("file-code")), title);
    if (!page->databaseContext().isEmpty()) {
        queryTabs_->setTabToolTip(
            index, QStringLiteral("PostgreSQL / %1")
                       .arg(page->databaseContext()));
    }
    auto *closeButton = new QToolButton(queryTabs_->tabBar());
    closeButton->setObjectName(QStringLiteral("tabCloseButton"));
    closeButton->setIcon(toolbarIcon(QStringLiteral("x")));
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(18, 18);
    closeButton->setToolTip(QStringLiteral("关闭查询（Ctrl+W）"));
    queryTabs_->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);
    connect(closeButton, &QToolButton::clicked, this, [this, page] {
        closeQuery(queryTabs_->indexOf(page));
    });
    connect(page, &QueryPage::commitRequested, this, [this, page] { applyPendingChanges(page); });
    queryTabs_->setCurrentIndex(index);
    page->editor()->document()->setModified(false);
    connect(page->editor()->document(), &QTextDocument::modificationChanged,
            this, [this, page](bool modified) {
        const int tabIndex = queryTabs_->indexOf(page);
        if (tabIndex < 0)
            return;
        const QString baseTitle = page->property("tabTitle").toString();
        queryTabs_->setTabText(tabIndex, modified ? QStringLiteral("* %1").arg(baseTitle) : baseTitle);
    });
    page->editor()->setFocus();
}

QueryPage *MainWindow::currentQuery() const
{
    return qobject_cast<QueryPage *>(queryTabs_->currentWidget());
}

void MainWindow::initializeQueryWorker()
{
    qRegisterMetaType<QVector<QueryColumn>>();
    qRegisterMetaType<QVector<QVariantList>>();

    queryThread_ = new QThread(this);
    queryWorker_ = new PostgresQueryWorker;
    queryWorker_->moveToThread(queryThread_);
    connect(queryThread_, &QThread::finished, queryWorker_, &QObject::deleteLater);

    connect(queryWorker_, &PostgresQueryWorker::backendReady, this,
            [this](quint64 requestId, qint64 backendPid) {
        if (requestId == runningQueryId_ && runningQueryPage_)
            runningBackendPid_ = backendPid;
    });
    connect(queryWorker_, &PostgresQueryWorker::resultSetReady, this,
            [this](quint64 requestId, QVector<QueryColumn> columns, bool select) {
        if (requestId != runningQueryId_ || !runningQueryPage_)
            return;
        runningQueryPage_->beginResult(std::move(columns), select,
                                       runningQueryEditable_ && select);
    });
    connect(queryWorker_, &PostgresQueryWorker::rowsReady, this,
            [this](quint64 requestId, QVector<QVariantList> rows, qint64 loadedBytes) {
        if (requestId != runningQueryId_ || !runningQueryPage_)
            return;
        runningQueryPage_->appendResultRows(std::move(rows), loadedBytes);
    });
    connect(queryWorker_, &PostgresQueryWorker::finished, this,
            [this](quint64 requestId, qlonglong affectedRows, bool truncated,
                   bool memoryLimited, bool cancelled, qint64 loadedBytes,
                   const QString &error) {
        if (requestId != runningQueryId_)
            return;
        QPointer<QueryPage> page = runningQueryPage_;
        runningQueryPage_.clear();
        runningBackendPid_ = -1;
        runningQueryEditable_ = false;
        if (!page)
            return;
        page->finishExecution(affectedRows, truncated, memoryLimited,
                              cancelled, loadedBytes, error);
        if (!error.isEmpty())
            showDatabaseError(QStringLiteral("SQL 执行失败"), error);
    });
    queryThread_->start();
}

void MainWindow::executeQuery()
{
    QueryPage *page = currentQuery();
    if (!page)
        return;
    page->clearTableContext();
    runQuery(page, false);
}

void MainWindow::runQuery(QueryPage *page, bool editableTable)
{
    if (!page)
        return;
    if (!postgres_.isConnected()) {
        page->setStatus(QStringLiteral("请先连接 PostgreSQL"));
        connectSelectedConnection();
        return;
    }
    const QString sql = page->selectedSql().trimmed();
    if (sql.isEmpty()) {
        page->setStatus(QStringLiteral("请输入要执行的 SQL"));
        return;
    }
    if (runningQueryPage_) {
        page->setStatus(runningQueryPage_ == page
            ? QStringLiteral("当前查询仍在执行，可按 Esc 取消")
            : QStringLiteral("另一项查询正在执行；完成后可继续执行"));
        return;
    }
    if (page->databaseContext().isEmpty()) {
        page->setDatabaseContext(postgres_.config().database);
        const int pageIndex = queryTabs_->indexOf(page);
        if (pageIndex >= 0) {
            queryTabs_->setTabToolTip(
                pageIndex, QStringLiteral("PostgreSQL / %1")
                               .arg(page->databaseContext()));
        }
    } else if (page->databaseContext()
               != postgres_.config().database) {
        QString error;
        if (!activatePostgresDatabase(
                page->databaseContext(), &error)) {
            showDatabaseError(
                QStringLiteral("无法切换到查询所属数据库"), error);
            page->setStatus(
                QStringLiteral("查询未执行：标签属于数据库 %1")
                    .arg(page->databaseContext()));
            return;
        }
    }

    page->beginExecution();
    runningQueryPage_ = page;
    runningBackendPid_ = -1;
    runningQueryEditable_ = editableTable;
    const quint64 requestId = ++runningQueryId_;

    PostgresQueryRequest request;
    request.id = requestId;
    request.config = postgres_.config();
    request.sessionUser = postgres_.currentUser();
    request.sql = sql;
    request.rowLimit = page->rowLimit();

    queryWorker_->prepareRequest();
    PostgresQueryWorker *worker = queryWorker_;
    QMetaObject::invokeMethod(worker,
        [worker, request = std::move(request)]() mutable {
            worker->execute(std::move(request));
        },
        Qt::QueuedConnection);
}

void MainWindow::cancelRunningQuery()
{
    if (!runningQueryPage_ || !queryWorker_)
        return;

    queryWorker_->requestCancel();
    runningQueryPage_->setStatus(QStringLiteral("正在取消查询…"));
    if (runningBackendPid_ > 0 && postgres_.isConnected()) {
        QString ignoredError;
        postgres_.cancelBackend(runningBackendPid_, &ignoredError);
    }
}

void MainWindow::stopQuery()
{
    QueryPage *page = currentQuery();
    if (!runningQueryPage_ || page != runningQueryPage_) {
        if (page)
            page->setStatus(QStringLiteral("当前标签没有执行中的查询"));
        return;
    }
    cancelRunningQuery();
}

void MainWindow::refreshSchema()
{
    const QString driver = selectedConnectionDriver();
    if (driver == QStringLiteral("redis")) {
        if (!redis_.isConnected()) {
            connectSelectedConnection();
            return;
        }
        populateSchema();
        selectRedisDatabaseInTree(redis_.config().database);
        statusBar()->showMessage(QStringLiteral("Redis 键空间已刷新"), 3000);
        return;
    }
    if (driver != QStringLiteral("postgresql"))
        return;
    if (!postgres_.isConnected()) {
        connectSelectedConnection();
        return;
    }
    populateSchema();
    selectPostgresDatabaseInTree(postgres_.config().database);
    statusBar()->showMessage(QStringLiteral("数据库结构已刷新"), 3000);
}

void MainWindow::closeQuery(int index)
{
    if (index < 0)
        return;
    QWidget *page = queryTabs_->widget(index);
    if (page == runningQueryPage_)
        cancelRunningQuery();
    queryTabs_->removeTab(index);
    page->deleteLater();
    if (queryTabs_->count() == 0)
        addQuery();
}

void MainWindow::handleSchemaSelection()
{
    const QModelIndex proxyIndex = schemaTree_->currentIndex();
    if (!proxyIndex.isValid())
        return;
    const QModelIndex source = schemaProxy_->mapToSource(proxyIndex);
    const QString name = source.data(NameRole).toString();
    const QString type = source.data(TypeRole).toString();
    const QString schema = source.data(SchemaRole).toString();
    const QString connectionId = connectionIdForIndex(source);
    const QString driver = driverForIndex(source);
    if (!driver.isEmpty()) {
        focusedDriver_ = driver;
        updateConnectionUi();
    }
    if (driver == QStringLiteral("redis")) {
        if (!connectionId.isEmpty()
            && (!redis_.isConnected()
                || connectionId != activeRedisConnectionId_)) {
            if (const SavedRedisConnection *connection =
                    savedRedisConnection(connectionId)) {
                updateInspector(*connection, false);
                return;
            }
        }
        updateInspector(name, type, schema);
        return;
    }
    if (!connectionId.isEmpty()
        && (!postgres_.isConnected() || connectionId != activeConnectionId_)) {
        if (const SavedConnection *connection = savedConnection(connectionId)) {
            updateInspector(*connection, false);
            return;
        }
    }
    const QString targetDatabase =
        postgresDatabaseForIndex(source);
    if (type != QStringLiteral("database")
        && !targetDatabase.isEmpty()
        && targetDatabase != postgres_.config().database) {
        QString error;
        if (!activatePostgresDatabase(targetDatabase, &error)) {
            showDatabaseError(
                QStringLiteral("切换 PostgreSQL 数据库失败"), error);
            return;
        }
    }
    if ((type == QStringLiteral("Table") || type == QStringLiteral("View"))
        && postgres_.isConnected()) {
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const DatabaseTable table = postgres_.describeTable(schema, name, &error);
        QApplication::restoreOverrideCursor();
        if (!error.isEmpty()) {
            showDatabaseError(QStringLiteral("读取关系结构失败"), error);
            return;
        }
        updateInspector(table);
        return;
    }
    updateInspector(name, type, schema);
}

void MainWindow::activateSchemaItem(const QModelIndex &proxyIndex)
{
    const QModelIndex source = schemaProxy_->mapToSource(proxyIndex);
    const QString type = source.data(TypeRole).toString();
    const QString name = source.data(NameRole).toString();
    const QString schema = source.data(SchemaRole).toString();
    const QString connectionId = connectionIdForIndex(source);
    const QString driver = driverForIndex(source);
    QString error;

    if (driver == QStringLiteral("redis")) {
        if (!connectionId.isEmpty()
            && (!redis_.isConnected()
                || connectionId != activeRedisConnectionId_)) {
            connectSavedRedisConnection(connectionId);
            return;
        }
        if (type == QStringLiteral("redis-connection")) {
            schemaTree_->setExpanded(proxyIndex,
                                     !schemaTree_->isExpanded(proxyIndex));
            return;
        }
        if (type == QStringLiteral("redis-database")) {
            const int database = source.data(RedisDatabaseRole).toInt();
            if (database == redis_.config().database) {
                schemaTree_->setExpanded(
                    proxyIndex, !schemaTree_->isExpanded(proxyIndex));
                return;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool selected = redis_.selectDatabase(database, &error);
            QApplication::restoreOverrideCursor();
            if (!selected) {
                showDatabaseError(QStringLiteral("切换 Redis 数据库失败"), error);
                return;
            }
            closeRedisPages();
            QString saveError;
            const bool saved = persistActiveRedisConnectionConfig(&saveError);
            populateSchema();
            selectRedisDatabaseInTree(database);
            statusBar()->showMessage(
                saved || activeRedisConnectionId_.isEmpty()
                    ? QStringLiteral("当前 Redis 数据库已切换为 db%1")
                          .arg(database)
                    : QStringLiteral("已切换到 db%1，但无法保存默认数据库：%2")
                          .arg(database).arg(saveError),
                6000);
            return;
        }
        if (type == QStringLiteral("redis-key")) {
            openRedisKey(source.data(RedisKeyRole).toByteArray());
            return;
        }
        if (type == QStringLiteral("redis-load-more")) {
            QStandardItem *more = schemaModel_->itemFromIndex(source);
            if (more && more->parent()) {
                loadRedisKeys(
                    more->parent(),
                    source.data(RedisCursorRole).toByteArray(), true);
            }
            return;
        }
        return;
    }

    if (!connectionId.isEmpty()
        && (!postgres_.isConnected() || connectionId != activeConnectionId_)) {
        connectSavedConnection(
            connectionId,
            type == QStringLiteral("database") ? name : QString{});
        return;
    }

    if (type == QStringLiteral("connection")) {
        if (postgres_.isConnected()) {
            schemaTree_->setExpanded(proxyIndex,
                                     !schemaTree_->isExpanded(proxyIndex));
        } else {
            showPostgresConnectionDialog();
        }
        return;
    }

    if (type == QStringLiteral("user")) {
        cancelRunningQuery();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool switched = postgres_.switchUser(name, &error);
        QApplication::restoreOverrideCursor();
        if (!switched) {
            showDatabaseError(QStringLiteral("切换用户失败"), error);
            return;
        }
        updateConnectionUi();
        populateSchema();
        statusBar()->showMessage(QStringLiteral("当前用户已切换为 %1").arg(name), 5000);
        return;
    }

    if (type == QStringLiteral("database")) {
        if (name == postgres_.config().database) {
            schemaTree_->expand(proxyIndex);
            return;
        }
        cancelRunningQuery();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool switched = postgres_.switchDatabase(name, &error);
        QApplication::restoreOverrideCursor();
        if (!switched) {
            showDatabaseError(QStringLiteral("切换数据库失败"), error);
            return;
        }
        QString saveError;
        const bool saved = persistActiveConnectionConfig(&saveError);
        updatePostgresDatabaseAppearance();
        updateConnectionUi();
        selectPostgresDatabaseInTree(name);
        QStandardItem *databaseItem =
            schemaModel_->itemFromIndex(source);
        if (databaseItem && databaseItem->rowCount() > 0) {
            QStandardItem *schemas = databaseItem->child(0);
            loadSchemaChildren(schemas);
            schemaTree_->expand(
                schemaProxy_->mapFromSource(schemas->index()));
        }
        statusBar()->showMessage(
            saved
                ? QStringLiteral("当前数据库已切换为 %1").arg(name)
                : QStringLiteral("已切换到 %1，但无法保存为默认数据库：%2")
                      .arg(name, saveError),
            6000);
        return;
    }

    if (type != QStringLiteral("Table") && type != QStringLiteral("View"))
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const DatabaseTable table = postgres_.describeTable(schema, name, &error);
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        showDatabaseError(QStringLiteral("读取关系结构失败"), error);
        return;
    }

    addQuery(postgres_.buildRelationPreview(table));
    QueryPage *page = currentQuery();
    const bool editable = type == QStringLiteral("Table") && !table.primaryKeys().isEmpty();
    if (editable)
        page->setTableContext(postgres_.config().database, schema,
                              name, table.primaryKeys());
    else
        page->clearTableContext();
    runQuery(page, editable);
}

void MainWindow::applyPendingChanges(QueryPage *page)
{
    if (!page || page->resultModel()->pendingChangeCount() == 0)
        return;
    QString error;
    ResultTableModel *model = page->resultModel();
    if (!page->tableDatabase().isEmpty()
        && page->tableDatabase() != postgres_.config().database
        && !activatePostgresDatabase(page->tableDatabase(), &error)) {
        showDatabaseError(
            QStringLiteral("无法切换到结果所属数据库"), error);
        page->setStatus(
            QStringLiteral("提交失败：结果属于数据库 %1")
                .arg(page->tableDatabase()));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool applied = postgres_.applyChanges(
        page->tableSchema(), page->tableName(), model->columns(), model->originalRows(),
        page->primaryKeys(), model->pendingChanges(), &error);
    QApplication::restoreOverrideCursor();
    if (!applied) {
        showDatabaseError(QStringLiteral("提交修改失败"), error);
        page->setStatus(QStringLiteral("提交失败：%1").arg(error));
        return;
    }
    const int count = model->pendingChangeCount();
    model->commitPendingChanges();
    page->setStatus(QStringLiteral("已在一个事务中提交 %1 处修改").arg(count));
}

void MainWindow::updateInspector(const QString &name, const QString &type,
                                 const QString &schema)
{
    QString iconName = QStringLiteral("boxes");
    QString displayType = type;
    QString path;
    if (type == QStringLiteral("redis-connection")) {
        iconName = QStringLiteral("redis");
        displayType = redis_.isConnected()
            ? QStringLiteral("Redis connection")
            : QStringLiteral("Disconnected");
        path = redis_.isConnected()
            ? redis_.config().displayName()
            : QStringLiteral("双击连接以开始");
        description_->setText(
            redis_.isConnected()
                ? QStringLiteral("TLS：%1 · 当前数据库：db%2")
                      .arg(redis_.config().tls ? QStringLiteral("已启用")
                                              : QStringLiteral("未启用"))
                      .arg(redis_.config().database)
                : QStringLiteral("—"));
    } else if (type == QStringLiteral("redis-database")) {
        iconName = QStringLiteral("database");
        displayType = QStringLiteral("Redis database");
        path = QStringLiteral("%1 / %2")
                   .arg(redis_.config().host, name);
    } else if (type == QStringLiteral("redis-databases")) {
        iconName = QStringLiteral("database");
        displayType = QStringLiteral("Redis databases");
        path = redis_.config().host;
    } else if (type == QStringLiteral("redis-keys")) {
        iconName = QStringLiteral("key-round");
        displayType = QStringLiteral("Redis keys");
        path = QStringLiteral("%1 / db%2")
                   .arg(redis_.config().host)
                   .arg(redis_.config().database);
    } else if (type == QStringLiteral("redis-key")) {
        iconName = QStringLiteral("key-round");
        displayType = QStringLiteral("Redis key");
        path = QStringLiteral("%1 / db%2 / %3")
                   .arg(redis_.config().host)
                   .arg(redis_.config().database)
                   .arg(name);
    } else if (type == QStringLiteral("connection")) {
        iconName = QStringLiteral("postgresql");
        displayType = postgres_.isConnected() ? QStringLiteral("PostgreSQL connection")
                                              : QStringLiteral("Disconnected");
        path = postgres_.isConnected()
            ? QStringLiteral("%1:%2/%3").arg(postgres_.config().host)
                  .arg(postgres_.config().port).arg(postgres_.config().database)
            : QStringLiteral("双击工具栏连接按钮以开始");
        description_->setText(postgres_.isConnected()
            ? QStringLiteral("当前用户：%1").arg(postgres_.currentUser()) : QStringLiteral("—"));
    } else if (type == QStringLiteral("database")) {
        iconName = QStringLiteral("database");
        displayType = QStringLiteral("Database");
        path = postgres_.config().host + QStringLiteral(" / ") + name;
    } else if (type == QStringLiteral("schema")) {
        iconName = QStringLiteral("boxes");
        displayType = QStringLiteral("Schema");
        path = postgres_.config().database + QStringLiteral(" / ") + name;
    } else if (type == QStringLiteral("user")) {
        iconName = QStringLiteral("key-round");
        displayType = QStringLiteral("PostgreSQL user");
        path = postgres_.config().host + QStringLiteral(" / users / ") + name;
    } else if (type == QStringLiteral("users")) {
        iconName = QStringLiteral("key-round");
        displayType = QStringLiteral("Users");
        path = postgres_.config().host + QStringLiteral(" / users");
    } else if (type == QStringLiteral("databases")) {
        iconName = QStringLiteral("database");
        displayType = QStringLiteral("Databases");
        path = postgres_.config().host + QStringLiteral(" / databases");
    } else if (type == QStringLiteral("tables")) {
        iconName = QStringLiteral("table-2");
        displayType = QStringLiteral("Tables");
        path = postgres_.config().database + QStringLiteral(" / ") + schema;
    } else if (type == QStringLiteral("views")) {
        iconName = QStringLiteral("eye");
        displayType = QStringLiteral("Views");
        path = postgres_.config().database + QStringLiteral(" / ") + schema;
    }

    objectIcon_->setPixmap(toolbarIcon(iconName).pixmap(QSize(20, 20)));
    objectName_->setText(name);
    objectType_->setText(displayType);
    objectPath_->setText(path);
    rowEstimate_->setText(QStringLiteral("—"));
    size_->setText(QStringLiteral("—"));
    if (type != QStringLiteral("connection")
        && type != QStringLiteral("redis-connection"))
        description_->setText(QStringLiteral("—"));
    clearInspectorModels();
}

void MainWindow::updateInspector(const SavedRedisConnection &connection,
                                 bool connected)
{
    const RedisConnectionConfig &config =
        connected ? redis_.config() : connection.config;
    objectIcon_->setPixmap(
        toolbarIcon(QStringLiteral("redis")).pixmap(QSize(20, 20)));
    objectName_->setText(config.displayName());
    objectType_->setText(
        connected ? QStringLiteral("Redis connection")
                  : QStringLiteral("Saved connection · Disconnected"));
    objectPath_->setText(config.displayName());
    rowEstimate_->setText(QStringLiteral("—"));
    size_->setText(QStringLiteral("—"));
    description_->setText(
        connected
            ? QStringLiteral("TLS：%1 · 当前数据库：db%2")
                  .arg(config.tls ? QStringLiteral("已启用")
                                  : QStringLiteral("未启用"))
                  .arg(config.database)
            : connection.hasStoredPassword
                ? QStringLiteral("双击左侧连接即可使用已保存凭据连接")
                : QStringLiteral("未找到已保存密码；双击后可重新输入"));
    clearInspectorModels();
}

void MainWindow::updateInspector(const SavedConnection &connection, bool connected)
{
    const PostgresConnectionConfig &config =
        connected ? postgres_.config() : connection.config;
    objectIcon_->setPixmap(toolbarIcon(QStringLiteral("postgresql")).pixmap(QSize(20, 20)));
    objectName_->setText(config.displayName());
    objectType_->setText(connected ? QStringLiteral("PostgreSQL connection")
                                   : QStringLiteral("Saved connection · Disconnected"));
    objectPath_->setText(
        QStringLiteral("%1:%2/%3")
            .arg(config.host)
            .arg(config.port)
            .arg(config.database));
    rowEstimate_->setText(QStringLiteral("—"));
    size_->setText(QStringLiteral("—"));
    description_->setText(
        connected
            ? QStringLiteral("当前用户：%1").arg(postgres_.currentUser())
            : connection.hasStoredPassword
                  ? QStringLiteral("双击左侧连接即可使用已保存凭据连接")
                  : QStringLiteral("未找到已保存密码；双击后可重新输入"));
    clearInspectorModels();
}

void MainWindow::updateInspector(const DatabaseTable &table)
{
    objectIcon_->setPixmap(toolbarIcon(QStringLiteral("table-2")).pixmap(QSize(20, 20)));
    objectName_->setText(table.name);
    objectType_->setText(QStringLiteral("PostgreSQL relation"));
    objectPath_->setText(QStringLiteral("%1 / %2.%3")
                             .arg(postgres_.config().database, table.schema, table.name));
    rowEstimate_->setText(table.estimatedRows >= 0
        ? QStringLiteral("约 %1").arg(table.estimatedRows) : QStringLiteral("—"));
    size_->setText(table.totalSize.isEmpty() ? QStringLiteral("—") : table.totalSize);
    description_->setText(table.description.isEmpty() ? QStringLiteral("—") : table.description);
    clearInspectorModels();

    for (const DatabaseColumn &column : table.columns) {
        appendReadOnlyRow(columnModel_,
                          {column.name, column.type, column.nullable ? QStringLiteral("YES")
                                                                   : QStringLiteral("NO"),
                           column.defaultValue.isEmpty() ? QStringLiteral("—") : column.defaultValue},
                          column.primaryKey ? QStringLiteral("key-round") : QString{});
    }
    for (const DatabaseIndex &index : table.indexes)
        appendReadOnlyRow(indexModel_, {index.name, index.definition}, QStringLiteral("key-round"));
    for (const DatabaseForeignKey &foreignKey : table.foreignKeys)
        appendReadOnlyRow(foreignKeyModel_, {foreignKey.name, foreignKey.definition},
                          QStringLiteral("key-round"));
}

void MainWindow::clearInspectorModels()
{
    columnModel_->removeRows(0, columnModel_->rowCount());
    indexModel_->removeRows(0, indexModel_->rowCount());
    foreignKeyModel_->removeRows(0, foreignKeyModel_->rowCount());
}

void MainWindow::updateConnectionUi()
{
    connectionContext_->clear();
    schemaContext_->clear();
    const QString driver = selectedConnectionDriver();
    bool driverConnected =
        driver == QStringLiteral("redis")
        ? redis_.isConnected()
        : driver == QStringLiteral("postgresql")
            ? postgres_.isConnected()
            : false;
    if (driver == QStringLiteral("postgresql")
        && schemaTree_->currentIndex().isValid()) {
        const QString database = postgresDatabaseForIndex(
            schemaProxy_->mapToSource(schemaTree_->currentIndex()));
        if (!database.isEmpty())
            driverConnected =
                postgres_.isDatabaseConnected(database);
    }
    reconnectAction_->setEnabled(driverConnected);
    disconnectAction_->setEnabled(driverConnected);

    if (driver == QStringLiteral("redis")) {
        if (redis_.isConnected()) {
            connectionContext_->addItem(
                toolbarIcon(QStringLiteral("redis")),
                redis_.config().displayName());
            schemaContext_->addItem(
                toolbarIcon(QStringLiteral("database")),
                QStringLiteral("db%1").arg(redis_.config().database));
            return;
        }
        const QString connectionId = selectedConnectionId().isEmpty()
            ? activeRedisConnectionId_ : selectedConnectionId();
        const SavedRedisConnection *connection =
            savedRedisConnection(connectionId);
        connectionContext_->addItem(
            toolbarIcon(QStringLiteral("redis")),
            connection
                ? QStringLiteral("%1 · 未连接")
                      .arg(connection->config.displayName())
                : QStringLiteral("Redis · 未连接"));
        schemaContext_->addItem(
            toolbarIcon(QStringLiteral("database")),
            QStringLiteral("无活动数据库"));
        return;
    }

    if (driver == QStringLiteral("postgresql")) {
        if (postgres_.isConnected()) {
            const QString label = QStringLiteral("%1@%2 / %3")
                                      .arg(postgres_.currentUser(),
                                           postgres_.config().host,
                                           postgres_.config().database);
            connectionContext_->addItem(
                toolbarIcon(QStringLiteral("postgresql")), label);
            schemaContext_->addItem(
                toolbarIcon(QStringLiteral("boxes")),
                QStringLiteral("public @ %1")
                    .arg(postgres_.config().database));
            return;
        }
        const QString connectionId = selectedConnectionId().isEmpty()
            ? activeConnectionId_ : selectedConnectionId();
        const SavedConnection *connection =
            savedConnection(connectionId);
        connectionContext_->addItem(
            toolbarIcon(QStringLiteral("postgresql")),
            connection
                ? QStringLiteral("%1 · 未连接")
                      .arg(connection->config.displayName())
                : QStringLiteral("PostgreSQL · 未连接"));
        schemaContext_->addItem(
            toolbarIcon(QStringLiteral("boxes")),
            QStringLiteral("无活动数据库"));
        return;
    }

    const int savedCount =
        savedConnections_.size() + savedRedisConnections_.size();
    connectionContext_->addItem(
        toolbarIcon(QStringLiteral("database-zap")),
        savedCount == 0
            ? QStringLiteral("数据库 · 未连接")
            : QStringLiteral("%1 个已保存连接").arg(savedCount));
    schemaContext_->addItem(
        toolbarIcon(QStringLiteral("boxes")),
        QStringLiteral("无活动数据库"));
}

void MainWindow::refreshConnectionPresentation()
{
    populateSchema();
    if (focusedDriver_ == QStringLiteral("postgresql")
        && postgres_.isConnected()) {
        selectPostgresDatabaseInTree(postgres_.config().database);
    } else if (focusedDriver_ == QStringLiteral("redis")
               && redis_.isConnected()) {
        selectRedisDatabaseInTree(redis_.config().database);
    }
    updateConnectionUi();
    if (focusedDriver_ == QStringLiteral("redis")) {
        if (const SavedRedisConnection *active =
                savedRedisConnection(activeRedisConnectionId_)) {
            updateInspector(*active, redis_.isConnected());
        } else if (redis_.isConnected()) {
            updateInspector(redis_.config().displayName(),
                            QStringLiteral("redis-connection"));
        }
        return;
    }
    if (focusedDriver_ == QStringLiteral("postgresql")) {
        if (const SavedConnection *active =
                savedConnection(activeConnectionId_)) {
            updateInspector(*active, postgres_.isConnected());
        } else if (postgres_.isConnected()) {
            updateInspector(QStringLiteral("PostgreSQL"),
                            QStringLiteral("connection"));
        }
        return;
    }
    updateInspector(QStringLiteral("Database"), QStringLiteral("connection"));
}

void MainWindow::showPostgresConnectionDialog()
{
    editConnection(QString{});
}

void MainWindow::showRedisConnectionDialog()
{
    editRedisConnection(QString{});
}

void MainWindow::editSelectedConnection()
{
    const QString driver = selectedConnectionDriver();
    if (driver == QStringLiteral("redis")
        || (driver.isEmpty() && redis_.isConnected())) {
        editRedisConnection(
            selectedConnectionId().isEmpty()
                ? activeRedisConnectionId_
                : selectedConnectionId());
        return;
    }
    editConnection(preferredConnectionId());
}

bool MainWindow::editConnection(
    const QString &connectionId, const QString &notice,
    const std::optional<PostgresConnectionConfig> &initialConfig)
{
    SavedConnection draft;
    bool hasInitialConfig = false;
    if (const SavedConnection *existing = savedConnection(connectionId)) {
        draft = *existing;
        hasInitialConfig = true;
    }
    if (initialConfig) {
        draft.config = *initialConfig;
        hasInitialConfig = true;
    }

    if (!notice.isEmpty())
        QMessageBox::warning(this, QStringLiteral("需要更新连接信息"), notice);

    PostgresConnectionConfig candidate = draft.config;
    while (true) {
        PostgresConnectionDialog dialog(this);
        if (hasInitialConfig)
            dialog.setConfig(candidate);
        if (dialog.exec() != QDialog::Accepted) {
            refreshConnectionPresentation();
            return false;
        }

        candidate = dialog.config();
        hasInitialConfig = true;
        cancelRunningQuery();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString connectionError;
        const bool connected = postgres_.connectToServer(candidate, &connectionError);
        QApplication::restoreOverrideCursor();
        if (!connected) {
            QMessageBox::warning(
                this, QStringLiteral("PostgreSQL 连接失败"),
                QStringLiteral("%1\n\n请修改密码、连接地址、端口、用户或数据库后重试。")
                    .arg(connectionError.isEmpty()
                             ? QStringLiteral("未知数据库错误")
                             : connectionError));
            continue;
        }

        draft.config = candidate;
        QSettings settings;
        QString saveError;
        const bool saved = ConnectionStore::upsert(
            settings, *credentialStore_, draft, &saveError);
        if (saved) {
            if (SavedConnection *existing = savedConnection(draft.id))
                *existing = draft;
            else
                savedConnections_.append(draft);
            activeConnectionId_ = draft.id;
        } else {
            QMessageBox::warning(
                this, QStringLiteral("连接已建立，但无法保存"),
                QStringLiteral("%1\n\n本次会话仍可使用该连接，但下次启动时需要重新输入。")
                    .arg(saveError));
            if (!draft.id.isEmpty()) {
                if (SavedConnection *existing = savedConnection(draft.id))
                    *existing = draft;
                activeConnectionId_ = draft.id;
            } else {
                activeConnectionId_.clear();
            }
        }

        focusedDriver_ = QStringLiteral("postgresql");
        refreshConnectionPresentation();
        statusBar()->showMessage(
            saved
                ? QStringLiteral("已连接并保存 %1").arg(postgres_.config().displayName())
                : QStringLiteral("已连接 %1（未保存）").arg(postgres_.config().displayName()),
            6000);
        return true;
    }
}

void MainWindow::connectSelectedConnection()
{
    const QString driver = selectedConnectionDriver();
    if (driver == QStringLiteral("redis")
        || (driver.isEmpty() && !activeRedisConnectionId_.isEmpty())) {
        const QString connectionId = selectedConnectionId().isEmpty()
            ? activeRedisConnectionId_ : selectedConnectionId();
        if (connectionId.isEmpty())
            showRedisConnectionDialog();
        else
            connectSavedRedisConnection(connectionId);
        return;
    }
    const QString connectionId = preferredConnectionId();
    if (connectionId.isEmpty()) {
        showPostgresConnectionDialog();
        return;
    }

    const QModelIndex sourceIndex =
        schemaProxy_->mapToSource(schemaTree_->currentIndex());
    const QString database =
        sourceIndex.data(TypeRole).toString() == QStringLiteral("database")
        ? sourceIndex.data(NameRole).toString()
        : QString{};
    connectSavedConnection(connectionId, database);
}

void MainWindow::connectSavedConnection(
    const QString &connectionId, const QString &database)
{
    const SavedConnection *stored = savedConnection(connectionId);
    if (!stored)
        return;
    if (postgres_.isConnected() && connectionId == activeConnectionId_) {
        if (!database.isEmpty()
            && database != postgres_.config().database) {
            QString error;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool activated =
                activatePostgresDatabase(database, &error);
            QApplication::restoreOverrideCursor();
            if (!activated) {
                showDatabaseError(
                    QStringLiteral("连接 PostgreSQL 数据库失败"),
                    error);
                return;
            }
            populateSchema();
            selectPostgresDatabaseInTree(database);
            statusBar()->showMessage(
                QStringLiteral("已连接并切换到 %1").arg(database),
                4000);
            return;
        }
        statusBar()->showMessage(QStringLiteral("该数据库已经连接"), 3000);
        return;
    }

    PostgresConnectionConfig config = stored->config;
    if (!database.isEmpty())
        config.database = database;
    if (!stored->hasStoredPassword) {
        editConnection(
            connectionId,
            QStringLiteral("没有找到此连接的已保存密码，请重新输入连接信息。"),
            config);
        return;
    }

    cancelRunningQuery();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool connected = postgres_.connectToServer(config, &error);
    QApplication::restoreOverrideCursor();
    if (!connected) {
        editConnection(
            connectionId,
            QStringLiteral("使用已保存的账号和密码自动连接失败：\n%1\n\n"
                           "请更新密码、连接地址、端口、用户或数据库。")
                .arg(error.isEmpty() ? QStringLiteral("未知数据库错误") : error),
            config);
        return;
    }

    activeConnectionId_ = connectionId;
    focusedDriver_ = QStringLiteral("postgresql");
    QString saveError;
    const bool saved = persistActiveConnectionConfig(&saveError);
    refreshConnectionPresentation();
    statusBar()->showMessage(
        saved
            ? QStringLiteral("已使用保存的凭据连接 %1").arg(config.displayName())
            : QStringLiteral("已连接 %1，但无法保存为默认数据库：%2")
                  .arg(config.displayName(), saveError),
        6000);
}

bool MainWindow::persistActiveConnectionConfig(QString *error)
{
    SavedConnection *connection = savedConnection(activeConnectionId_);
    if (!connection) {
        if (error)
            *error = QStringLiteral("当前连接尚未保存。");
        return false;
    }
    connection->config = postgres_.config();
    QSettings settings;
    return ConnectionStore::upsert(
        settings, *credentialStore_, *connection, error);
}

bool MainWindow::editRedisConnection(
    const QString &connectionId, const QString &notice,
    const std::optional<RedisConnectionConfig> &initialConfig)
{
    SavedRedisConnection draft;
    bool hasInitialConfig = false;
    if (const SavedRedisConnection *existing =
            savedRedisConnection(connectionId)) {
        draft = *existing;
        hasInitialConfig = true;
    }
    if (initialConfig) {
        draft.config = *initialConfig;
        hasInitialConfig = true;
    }

    if (!notice.isEmpty())
        QMessageBox::warning(this, QStringLiteral("需要更新 Redis 连接信息"), notice);

    RedisConnectionConfig candidate = draft.config;
    while (true) {
        RedisConnectionDialog dialog(this);
        if (hasInitialConfig)
            dialog.setConfig(candidate);
        if (dialog.exec() != QDialog::Accepted) {
            refreshConnectionPresentation();
            return false;
        }

        candidate = dialog.config();
        hasInitialConfig = true;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString connectionError;
        const bool connected =
            redis_.connectToServer(candidate, &connectionError);
        QApplication::restoreOverrideCursor();
        if (!connected) {
            QMessageBox::warning(
                this, QStringLiteral("Redis 连接失败"),
                QStringLiteral("%1\n\n请检查连接地址、TLS、ACL 用户、密码和数据库编号。")
                    .arg(connectionError.isEmpty()
                             ? QStringLiteral("未知 Redis 错误")
                             : connectionError));
            continue;
        }

        closeRedisPages();
        draft.config = candidate;
        QSettings settings;
        QString saveError;
        const bool saved = RedisConnectionStore::upsert(
            settings, *credentialStore_, draft, &saveError);
        if (saved) {
            if (SavedRedisConnection *existing =
                    savedRedisConnection(draft.id)) {
                *existing = draft;
            } else {
                savedRedisConnections_.append(draft);
            }
            activeRedisConnectionId_ = draft.id;
        } else {
            QMessageBox::warning(
                this, QStringLiteral("连接已建立，但无法保存"),
                QStringLiteral("%1\n\n本次会话仍可使用该连接；密码不会明文写入设置。")
                    .arg(saveError));
            if (!draft.id.isEmpty()) {
                if (SavedRedisConnection *existing =
                        savedRedisConnection(draft.id)) {
                    *existing = draft;
                }
                activeRedisConnectionId_ = draft.id;
            } else {
                activeRedisConnectionId_.clear();
            }
        }

        focusedDriver_ = QStringLiteral("redis");
        refreshConnectionPresentation();
        statusBar()->showMessage(
            saved
                ? QStringLiteral("已连接并安全保存 %1")
                      .arg(redis_.config().displayName())
                : QStringLiteral("已连接 %1（未保存）")
                      .arg(redis_.config().displayName()),
            6000);
        return true;
    }
}

void MainWindow::connectSavedRedisConnection(const QString &connectionId)
{
    const SavedRedisConnection *stored =
        savedRedisConnection(connectionId);
    if (!stored)
        return;
    if (redis_.isConnected()
        && connectionId == activeRedisConnectionId_) {
        statusBar()->showMessage(QStringLiteral("该 Redis 已经连接"), 3000);
        return;
    }
    if (!stored->hasStoredPassword) {
        editRedisConnection(
            connectionId,
            QStringLiteral("没有找到此 Redis 连接的已保存密码，请重新输入连接信息。"),
            stored->config);
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool connected =
        redis_.connectToServer(stored->config, &error);
    QApplication::restoreOverrideCursor();
    if (!connected) {
        editRedisConnection(
            connectionId,
            QStringLiteral("使用已保存凭据自动连接 Redis 失败：\n%1")
                .arg(error.isEmpty() ? QStringLiteral("未知 Redis 错误")
                                     : error),
            stored->config);
        return;
    }

    closeRedisPages();
    activeRedisConnectionId_ = connectionId;
    focusedDriver_ = QStringLiteral("redis");
    refreshConnectionPresentation();
    statusBar()->showMessage(
        QStringLiteral("已使用保存的凭据连接 %1")
            .arg(stored->config.displayName()),
        6000);
}

bool MainWindow::persistActiveRedisConnectionConfig(QString *error)
{
    SavedRedisConnection *connection =
        savedRedisConnection(activeRedisConnectionId_);
    if (!connection) {
        if (error)
            *error = QStringLiteral("当前 Redis 连接尚未保存。");
        return false;
    }
    connection->config = redis_.config();
    QSettings settings;
    return RedisConnectionStore::upsert(
        settings, *credentialStore_, *connection, error);
}

void MainWindow::createRedisKey()
{
    if (!redis_.isConnected()) {
        statusBar()->showMessage(QStringLiteral("请先连接 Redis"), 4000);
        return;
    }
    bool accepted = false;
    const QString typeLabel = QInputDialog::getItem(
        this, QStringLiteral("新建 Redis 键"),
        QStringLiteral("数据类型"),
        {QStringLiteral("String"), QStringLiteral("Hash"),
         QStringLiteral("List"), QStringLiteral("Set"),
         QStringLiteral("Sorted Set"), QStringLiteral("Stream")},
        0, false, &accepted);
    if (!accepted)
        return;

    QByteArray type = typeLabel.toLower().toLatin1();
    if (typeLabel == QStringLiteral("Sorted Set"))
        type = QByteArrayLiteral("zset");
    const QString keyText = QInputDialog::getText(
        this, QStringLiteral("新建 Redis 键"),
        QStringLiteral("键名"), QLineEdit::Normal, {}, &accepted);
    if (!accepted || keyText.isEmpty())
        return;

    QByteArray identity;
    QByteArray value;
    if (type == QByteArrayLiteral("string")) {
        const QString valueText = QInputDialog::getMultiLineText(
            this, QStringLiteral("新建 Redis String"),
            QStringLiteral("值"), {}, &accepted);
        if (!accepted)
            return;
        value = valueText.toUtf8();
    } else if (type == QByteArrayLiteral("list")) {
        const QString listValue = QInputDialog::getMultiLineText(
            this, QStringLiteral("新建 Redis List"),
            QStringLiteral("第一个元素"), {}, &accepted);
        if (!accepted || listValue.isEmpty())
            return;
        identity = listValue.toUtf8();
    } else {
        const QString identityLabel =
            type == QByteArrayLiteral("hash")
                || type == QByteArrayLiteral("stream")
            ? QStringLiteral("第一个字段")
            : QStringLiteral("第一个成员");
        const QString identityText = QInputDialog::getText(
            this, QStringLiteral("新建 Redis 键"),
            identityLabel, QLineEdit::Normal, {}, &accepted);
        if (!accepted || identityText.isEmpty())
            return;
        identity = identityText.toUtf8();
        if (type == QByteArrayLiteral("hash")
            || type == QByteArrayLiteral("stream")) {
            const QString initialValue = QInputDialog::getMultiLineText(
                this, QStringLiteral("新建 Redis 键"),
                QStringLiteral("初始值"), {}, &accepted);
            if (!accepted)
                return;
            value = initialValue.toUtf8();
        } else if (type == QByteArrayLiteral("zset")) {
            const double score = QInputDialog::getDouble(
                this, QStringLiteral("新建 Redis Sorted Set"),
                QStringLiteral("初始分数"), 0.0, -1e100, 1e100, 6,
                &accepted);
            if (!accepted)
                return;
            value = QByteArray::number(score, 'g', 16);
        }
    }

    const int ttlSeconds = QInputDialog::getInt(
        this, QStringLiteral("新建 Redis 键"),
        QStringLiteral("TTL（秒，0 表示永久）"), 0, 0, 2147483, 1,
        &accepted);
    if (!accepted)
        return;

    QString error;
    const QByteArray key = keyText.toUtf8();
    const qint64 ttlMilliseconds =
        static_cast<qint64>(ttlSeconds) * 1000;
    const bool created =
        type == QByteArrayLiteral("string")
        ? redis_.createString(key, value, ttlMilliseconds, &error)
        : redis_.createCollection(
              type, key, identity, value, ttlMilliseconds, &error);
    if (!created) {
        showDatabaseError(QStringLiteral("新建 Redis 键失败"), error);
        return;
    }
    populateSchema();
    openRedisKey(key);
}

void MainWindow::filterRedisKeys()
{
    if (!redis_.isConnected())
        return;
    bool accepted = false;
    const QString currentPattern =
        redisBytesAreText(redisKeyPattern_)
        ? QString::fromUtf8(redisKeyPattern_) : QStringLiteral("*");
    const QString pattern = QInputDialog::getText(
        this, QStringLiteral("筛选 Redis 键"),
        QStringLiteral("SCAN MATCH 模式（例如 nexus:*）"),
        QLineEdit::Normal, currentPattern, &accepted);
    if (!accepted)
        return;
    const QByteArray encodedPattern =
        pattern.isEmpty() ? QByteArrayLiteral("*") : pattern.toUtf8();
    if (encodedPattern.size() > 1024) {
        QMessageBox::warning(
            this, QStringLiteral("筛选模式过长"),
            QStringLiteral("Redis 键筛选模式不能超过 1024 字节。"));
        return;
    }
    redisKeyPattern_ = encodedPattern;
    populateSchema();
    statusBar()->showMessage(
        redisKeyPattern_ == QByteArrayLiteral("*")
            ? QStringLiteral("已清除 Redis 键筛选")
            : QStringLiteral("正在显示匹配 %1 的 Redis 键")
                  .arg(redisDisplayBytes(redisKeyPattern_, 80)),
        5000);
}

void MainWindow::openRedisKey(const QByteArray &key)
{
    if (key.isEmpty() || !redis_.isConnected())
        return;
    for (int index = 0; index < queryTabs_->count(); ++index) {
        auto *existing =
            qobject_cast<RedisKeyPage *>(queryTabs_->widget(index));
        if (existing && existing->key() == key) {
            queryTabs_->setCurrentIndex(index);
            existing->refresh();
            return;
        }
    }

    auto *page = new RedisKeyPage(&redis_, key, queryTabs_);
    const int index = queryTabs_->addTab(
        page, toolbarIcon(QStringLiteral("redis")),
        redisDisplayBytes(key, 32));
    auto *closeButton = new QToolButton(queryTabs_->tabBar());
    closeButton->setObjectName(QStringLiteral("tabCloseButton"));
    closeButton->setIcon(toolbarIcon(QStringLiteral("x")));
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(18, 18);
    queryTabs_->tabBar()->setTabButton(
        index, QTabBar::RightSide, closeButton);
    connect(closeButton, &QToolButton::clicked, this, [this, page] {
        closeQuery(queryTabs_->indexOf(page));
    });
    connect(page, &RedisKeyPage::keyDeleted, this,
            [this, page](const QByteArray &) {
        closeQuery(queryTabs_->indexOf(page));
        populateSchema();
        statusBar()->showMessage(QStringLiteral("Redis 键已删除"), 4000);
    });
    connect(page, &RedisKeyPage::keyRenamed, this,
            [this, page](const QByteArray &, const QByteArray &newKey) {
        const int tabIndex = queryTabs_->indexOf(page);
        if (tabIndex >= 0)
            queryTabs_->setTabText(tabIndex, redisDisplayBytes(newKey, 32));
        populateSchema();
    });
    connect(page, &RedisKeyPage::keyMutated, this, [this] {
        statusBar()->showMessage(QStringLiteral("Redis 数据已更新"), 3000);
    });
    queryTabs_->setCurrentIndex(index);
}

void MainWindow::closeRedisPages()
{
    bool removed = false;
    for (int index = queryTabs_->count() - 1; index >= 0; --index) {
        QWidget *page = queryTabs_->widget(index);
        if (!qobject_cast<RedisKeyPage *>(page))
            continue;
        queryTabs_->removeTab(index);
        page->deleteLater();
        removed = true;
    }
    if (removed && queryTabs_->count() == 0)
        addQuery();
}

void MainWindow::showConnectionContextMenu(const QPoint &position)
{
    const QModelIndex proxyIndex = schemaTree_->indexAt(position);
    if (!proxyIndex.isValid())
        return;
    schemaTree_->setCurrentIndex(proxyIndex);
    const QModelIndex sourceIndex =
        schemaProxy_->mapToSource(proxyIndex);
    const QString driver = driverForIndex(sourceIndex);
    const QString type = sourceIndex.data(TypeRole).toString();
    const QString connectionId = selectedConnectionId();

    if (driver == QStringLiteral("redis")) {
        QMenu menu(schemaTree_);
        if (type == QStringLiteral("redis-key")) {
            const QByteArray key =
                sourceIndex.data(RedisKeyRole).toByteArray();
            QAction *openAction = menu.addAction(QStringLiteral("打开键"));
            connect(openAction, &QAction::triggered, this,
                    [this, key] { openRedisKey(key); });
            QAction *deleteAction = menu.addAction(QStringLiteral("删除键"));
            connect(deleteAction, &QAction::triggered, this, [this, key] {
                if (QMessageBox::question(
                        this, QStringLiteral("删除 Redis 键"),
                        QStringLiteral("确定删除 %1？此操作无法撤销。")
                            .arg(redisDisplayBytes(key, 180)))
                    != QMessageBox::Yes) {
                    return;
                }
                QString error;
                if (!redis_.deleteKey(key, &error)) {
                    showDatabaseError(QStringLiteral("删除 Redis 键失败"), error);
                    return;
                }
                populateSchema();
            });
            menu.addSeparator();
        }
        if (redis_.isConnected()) {
            QAction *createAction =
                menu.addAction(QStringLiteral("新建 Redis 键"));
            if (type == QStringLiteral("redis-database")) {
                createAction->setEnabled(
                    sourceIndex.data(RedisDatabaseRole).toInt()
                    == redis_.config().database);
            }
            connect(createAction, &QAction::triggered,
                    this, &MainWindow::createRedisKey);
            QAction *filterAction =
                menu.addAction(QStringLiteral("筛选键…"));
            if (type == QStringLiteral("redis-database")) {
                filterAction->setEnabled(
                    sourceIndex.data(RedisDatabaseRole).toInt()
                    == redis_.config().database);
            }
            connect(filterAction, &QAction::triggered,
                    this, &MainWindow::filterRedisKeys);
            QAction *refreshAction =
                menu.addAction(QStringLiteral("刷新键空间"));
            connect(refreshAction, &QAction::triggered,
                    this, &MainWindow::refreshSchema);
        }
        if (!connectionId.isEmpty()
            && (type == QStringLiteral("redis-connection")
                || menu.actions().isEmpty())) {
            if (!menu.actions().isEmpty())
                menu.addSeparator();
            QAction *connectAction = menu.addAction(QStringLiteral("连接"));
            connectAction->setEnabled(
                !redis_.isConnected()
                || connectionId != activeRedisConnectionId_);
            connect(connectAction, &QAction::triggered,
                    this, &MainWindow::connectSelectedConnection);
            QAction *editAction =
                menu.addAction(QStringLiteral("编辑连接"));
            connect(editAction, &QAction::triggered,
                    this, &MainWindow::editSelectedConnection);
            menu.addSeparator();
            QAction *removeAction =
                menu.addAction(QStringLiteral("删除已保存连接"));
            connect(removeAction, &QAction::triggered,
                    this, &MainWindow::removeSelectedConnection);
        }
        if (!menu.actions().isEmpty())
            menu.exec(schemaTree_->viewport()->mapToGlobal(position));
        return;
    }

    if (connectionId.isEmpty())
        return;

    QMenu menu(schemaTree_);
    QAction *connectAction = menu.addAction(QStringLiteral("连接"));
    connectAction->setEnabled(
        !postgres_.isConnected() || connectionId != activeConnectionId_);
    connect(connectAction, &QAction::triggered,
            this, &MainWindow::connectSelectedConnection);
    QAction *editAction = menu.addAction(QStringLiteral("编辑连接"));
    connect(editAction, &QAction::triggered,
            this, &MainWindow::editSelectedConnection);
    menu.addSeparator();
    QAction *removeAction = menu.addAction(QStringLiteral("删除已保存连接"));
    connect(removeAction, &QAction::triggered,
            this, &MainWindow::removeSelectedConnection);
    menu.exec(schemaTree_->viewport()->mapToGlobal(position));
}

void MainWindow::removeSelectedConnection()
{
    const QString connectionId = selectedConnectionId();
    if (selectedConnectionDriver() == QStringLiteral("redis")) {
        SavedRedisConnection *connection =
            savedRedisConnection(connectionId);
        if (!connection) {
            statusBar()->showMessage(
                QStringLiteral("请先选择要删除的已保存 Redis 连接"), 4000);
            return;
        }
        const QString displayName = connection->config.displayName();
        if (QMessageBox::question(
                this, QStringLiteral("删除已保存 Redis 连接"),
                QStringLiteral("确定删除 %1？\n保存的密码也会从系统凭据存储中移除。")
                    .arg(displayName))
            != QMessageBox::Yes) {
            return;
        }
        QSettings settings;
        QString error;
        if (!RedisConnectionStore::remove(
                settings, *credentialStore_, connectionId, &error)) {
            showDatabaseError(QStringLiteral("删除 Redis 连接失败"), error);
            return;
        }
        if (connectionId == activeRedisConnectionId_) {
            closeRedisPages();
            redis_.disconnect();
            activeRedisConnectionId_.clear();
        }
        for (qsizetype index = 0;
             index < savedRedisConnections_.size(); ++index) {
            if (savedRedisConnections_.at(index).id == connectionId) {
                savedRedisConnections_.removeAt(index);
                break;
            }
        }
        refreshConnectionPresentation();
        statusBar()->showMessage(
            QStringLiteral("已删除 %1").arg(displayName), 5000);
        return;
    }

    SavedConnection *connection = savedConnection(connectionId);
    if (!connection) {
        statusBar()->showMessage(QStringLiteral("请先选择要删除的已保存连接"), 4000);
        return;
    }
    const QString displayName = connection->config.displayName();
    if (QMessageBox::question(
            this, QStringLiteral("删除已保存连接"),
            QStringLiteral("确定删除 %1？\n保存的密码也会从 Windows 凭据管理器中移除。")
                .arg(displayName))
        != QMessageBox::Yes) {
        return;
    }

    QSettings settings;
    QString error;
    if (!ConnectionStore::remove(settings, *credentialStore_, connectionId, &error)) {
        showDatabaseError(QStringLiteral("删除连接失败"), error);
        return;
    }
    if (connectionId == activeConnectionId_) {
        cancelRunningQuery();
        postgres_.disconnect();
        activeConnectionId_.clear();
    }
    for (qsizetype index = 0; index < savedConnections_.size(); ++index) {
        if (savedConnections_.at(index).id == connectionId) {
            savedConnections_.removeAt(index);
            break;
        }
    }
    refreshConnectionPresentation();
    statusBar()->showMessage(QStringLiteral("已删除 %1").arg(displayName), 5000);
}

QString MainWindow::connectionIdForIndex(QModelIndex sourceIndex) const
{
    while (sourceIndex.isValid()) {
        const QString connectionId =
            sourceIndex.data(ConnectionIdRole).toString();
        if (!connectionId.isEmpty())
            return connectionId;
        sourceIndex = sourceIndex.parent();
    }
    return {};
}

QString MainWindow::driverForIndex(QModelIndex sourceIndex) const
{
    while (sourceIndex.isValid()) {
        const QString driver = sourceIndex.data(DriverRole).toString();
        if (!driver.isEmpty())
            return driver;
        sourceIndex = sourceIndex.parent();
    }
    return {};
}

QString MainWindow::selectedConnectionId() const
{
    const QModelIndex proxyIndex = schemaTree_->currentIndex();
    if (!proxyIndex.isValid())
        return {};
    return connectionIdForIndex(
        schemaProxy_->mapToSource(proxyIndex));
}

QString MainWindow::selectedConnectionDriver() const
{
    const QModelIndex proxyIndex = schemaTree_->currentIndex();
    if (proxyIndex.isValid()) {
        const QString driver = driverForIndex(
            schemaProxy_->mapToSource(proxyIndex));
        if (!driver.isEmpty())
            return driver;
    }
    if (!focusedDriver_.isEmpty())
        return focusedDriver_;
    if (postgres_.isConnected() || !activeConnectionId_.isEmpty())
        return QStringLiteral("postgresql");
    if (redis_.isConnected() || !activeRedisConnectionId_.isEmpty())
        return QStringLiteral("redis");
    return {};
}

QString MainWindow::preferredConnectionId() const
{
    const QString selected = selectedConnectionId();
    if (!selected.isEmpty()
        && selectedConnectionDriver() == QStringLiteral("postgresql"))
        return selected;
    if (!activeConnectionId_.isEmpty())
        return activeConnectionId_;
    return savedConnections_.isEmpty() ? QString{} : savedConnections_.constFirst().id;
}

SavedConnection *MainWindow::savedConnection(const QString &connectionId)
{
    for (SavedConnection &connection : savedConnections_) {
        if (connection.id == connectionId)
            return &connection;
    }
    return nullptr;
}

const SavedConnection *MainWindow::savedConnection(
    const QString &connectionId) const
{
    for (const SavedConnection &connection : savedConnections_) {
        if (connection.id == connectionId)
            return &connection;
    }
    return nullptr;
}

SavedRedisConnection *MainWindow::savedRedisConnection(
    const QString &connectionId)
{
    for (SavedRedisConnection &connection : savedRedisConnections_) {
        if (connection.id == connectionId)
            return &connection;
    }
    return nullptr;
}

const SavedRedisConnection *MainWindow::savedRedisConnection(
    const QString &connectionId) const
{
    for (const SavedRedisConnection &connection : savedRedisConnections_) {
        if (connection.id == connectionId)
            return &connection;
    }
    return nullptr;
}

void MainWindow::disconnectActiveConnection()
{
    const QString driver = selectedConnectionDriver();
    if (driver == QStringLiteral("redis") && redis_.isConnected()) {
        closeRedisPages();
        redis_.disconnect();
        return;
    }
    if (driver == QStringLiteral("postgresql") && postgres_.isConnected()) {
        cancelRunningQuery();
        const QModelIndex sourceIndex = schemaProxy_->mapToSource(
            schemaTree_->currentIndex());
        const QString database =
            postgresDatabaseForIndex(sourceIndex);
        if (!database.isEmpty()) {
            if (!postgres_.disconnectDatabase(database))
                return;
            if (postgres_.isConnected()
                && !activeConnectionId_.isEmpty()) {
                QString saveError;
                if (!persistActiveConnectionConfig(&saveError)) {
                    statusBar()->showMessage(
                        QStringLiteral(
                            "数据库已断开，但无法保存新的默认数据库：%1")
                            .arg(saveError),
                        5000);
                }
            }
            return;
        }
        postgres_.disconnect();
    }
}

void MainWindow::showDatabaseError(const QString &title, const QString &error)
{
    QMessageBox::critical(this, title, error.isEmpty() ? QStringLiteral("未知数据库错误") : error);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/mainSplitter"), mainSplitter_->saveState());
    QMainWindow::closeEvent(event);
}

} // namespace vsdb

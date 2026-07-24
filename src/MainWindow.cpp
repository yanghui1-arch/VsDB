#include "MainWindow.h"

#include "SqlEditor.h"
#include "UiComponents.h"
#include "WorkbenchModels.h"
#include "database/postgres/PostgresQueryWorker.h"
#include "ui/IconProvider.h"
#include "ui/PostgresConnectionDialog.h"
#include "ui/QueryPage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
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
    if (!iconName.isEmpty())
        item->setIcon(toolbarIcon(iconName));
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
    setWindowTitle(QStringLiteral("VsDB — PostgreSQL 数据库工作台"));
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

    populateSchema();
    installActions();
    initializeQueryWorker();
    addQuery(kInitialSql);
    if (!savedConnections_.isEmpty())
        updateInspector(savedConnections_.constFirst(), false);
    else
        updateInspector(QStringLiteral("PostgreSQL"), QStringLiteral("connection"));
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
    updateConnectionUi();
    populateSchema();
    updateInspector(QStringLiteral("PostgreSQL"), QStringLiteral("connection"));
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
        page->setTableContext(schema, relation, table.primaryKeys());
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
    newConnection->setToolTip(QStringLiteral("新建 PostgreSQL 连接"));
    newConnection->setPopupMode(QToolButton::MenuButtonPopup);
    auto *connectionMenu = new QMenu(newConnection);
    QAction *postgresAction = connectionMenu->addAction(
        toolbarIcon(QStringLiteral("postgresql")), QStringLiteral("PostgreSQL"));
    connect(postgresAction, &QAction::triggered, this, &MainWindow::showPostgresConnectionDialog);
    connect(newConnection, &QToolButton::clicked, this, &MainWindow::showPostgresConnectionDialog);
    newConnection->setMenu(connectionMenu);
    toolbar->addWidget(newConnection);

    QAction *manage = addAction(QStringLiteral("cloud-cog"), QStringLiteral("连接设置"),
                                QStringLiteral("编辑选中的 PostgreSQL 连接"));
    connect(manage, &QAction::triggered, this, &MainWindow::editSelectedConnection);
    toolbar->addSeparator();

    QAction *connectAction = addAction(QStringLiteral("plug"), QStringLiteral("连接"),
                                       QStringLiteral("连接选中的 PostgreSQL 数据库"));
    connect(connectAction, &QAction::triggered, this, &MainWindow::connectSelectedConnection);
    reconnectAction_ = addAction(QStringLiteral("refresh-cw"), QStringLiteral("重新连接"),
                                 QStringLiteral("重新连接并刷新结构"));
    connect(reconnectAction_, &QAction::triggered, this, [this] {
        cancelRunningQuery();
        QString error;
        if (!postgres_.reconnect(&error)) {
            if (!activeConnectionId_.isEmpty()) {
                editConnection(
                    activeConnectionId_,
                    QStringLiteral("使用已保存凭据重新连接失败：\n%1\n\n请更新连接信息后重试。")
                        .arg(error));
            } else {
                showDatabaseError(QStringLiteral("重新连接失败"), error);
            }
            return;
        }
        updateConnectionUi();
        populateSchema();
        statusBar()->showMessage(QStringLiteral("PostgreSQL 已重新连接"), 4000);
    });
    disconnectAction_ = addAction(QStringLiteral("unplug"), QStringLiteral("断开连接"),
                                  QStringLiteral("断开 PostgreSQL 连接"));
    connect(disconnectAction_, &QAction::triggered, this, [this] {
        cancelRunningQuery();
        postgres_.disconnect();
        updateConnectionUi();
        populateSchema();
        if (const SavedConnection *connection = savedConnection(activeConnectionId_))
            updateInspector(*connection, false);
        else
            updateInspector(QStringLiteral("PostgreSQL"), QStringLiteral("connection"));
        statusBar()->showMessage(QStringLiteral("PostgreSQL 已断开"), 4000);
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
    layout->addWidget(schemaTree_, 1);

    auto *footer = new QWidget(pane);
    footer->setFixedHeight(43);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(9, 3, 9, 3);
    QToolButton *add = plainButton(footer, QStringLiteral("＋"), QStringLiteral("添加 PostgreSQL 连接"));
    QToolButton *refresh = plainButton(footer, QStringLiteral("↻"), QStringLiteral("刷新数据库结构"));
    QToolButton *remove = plainButton(footer, QStringLiteral("⌫"), QStringLiteral("删除选中的已保存连接"));
    footerLayout->addWidget(add);
    footerLayout->addWidget(refresh);
    footerLayout->addWidget(remove);
    footerLayout->addStretch();
    layout->addWidget(footer);
    connect(add, &QToolButton::clicked, this, &MainWindow::showPostgresConnectionDialog);
    connect(refresh, &QToolButton::clicked, this, &MainWindow::refreshSchema);
    connect(remove, &QToolButton::clicked, this, &MainWindow::removeSelectedConnection);

    connect(schemaTree_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::handleSchemaSelection);
    connect(schemaTree_, &QTreeView::doubleClicked, this, &MainWindow::activateSchemaItem);
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

    for (const SavedConnection &connection : savedConnections_) {
        const bool active = postgres_.isConnected()
            && connection.id == activeConnectionId_;
        const PostgresConnectionConfig &displayConfig =
            active ? postgres_.config() : connection.config;
        const QString text = active
            ? QStringLiteral("PostgreSQL · %1（已连接）")
                  .arg(displayConfig.displayName())
            : QStringLiteral("%1（双击连接）").arg(displayConfig.displayName());
        auto *root = schemaItem(text, QStringLiteral("connection"),
                                QStringLiteral("postgresql"),
                                displayConfig.displayName());
        root->setData(connection.id, ConnectionIdRole);
        root->setToolTip(active
            ? QStringLiteral("当前已连接")
            : QStringLiteral("双击后使用已保存的账号和密码自动连接"));
        schemaModel_->appendRow(root);
        if (!selectedRoot)
            selectedRoot = root;
        if (connection.id == activeConnectionId_)
            selectedRoot = root;
        if (!active)
            continue;

        activeRoot = root;
        selectedRoot = root;
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
                if (schema->data(NameRole).toString() == QStringLiteral("public")) {
                    loadSchemaChildren(schema);
                    schemaTree_->expand(schemaProxy_->mapFromSource(schema->index()));
                    for (int relationGroup = 0; relationGroup < schema->rowCount();
                         ++relationGroup) {
                        schemaTree_->expand(schemaProxy_->mapFromSource(
                            schema->child(relationGroup)->index()));
                    }
                    break;
                }
            }
            break;
        }
    }

    if (postgres_.isConnected() && !activeRoot) {
        auto *root = schemaItem(
            QStringLiteral("PostgreSQL · %1（本次会话）")
                .arg(postgres_.config().displayName()),
            QStringLiteral("connection"), QStringLiteral("postgresql"),
            postgres_.config().displayName());
        schemaModel_->appendRow(root);
        selectedRoot = root;
    } else if (!selectedRoot) {
        selectedRoot = schemaItem(
            QStringLiteral("尚无已保存连接，点击下方＋添加"),
            QStringLiteral("empty"), QStringLiteral("postgresql"),
            QStringLiteral("PostgreSQL"));
        schemaModel_->appendRow(selectedRoot);
    }

    schemaTree_->setCurrentIndex(
        schemaProxy_->mapFromSource(selectedRoot->index()));
}

void MainWindow::loadSchemaChildren(QStandardItem *item)
{
    if (!item || item->data(LoadedRole).toBool() || !postgres_.isConnected())
        return;

    const QString type = item->data(TypeRole).toString();
    QString error;
    item->setData(true, LoadedRole);
    item->removeRows(0, item->rowCount());

    if (type == QStringLiteral("users")) {
        const QStringList users = postgres_.users(&error);
        for (const QString &user : users) {
            const bool current = user == postgres_.currentUser();
            QStandardItem *child = schemaItem(current ? QStringLiteral("%1（当前）").arg(user) : user,
                                              QStringLiteral("user"), QStringLiteral("key-round"), user);
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
            QStandardItem *database = schemaItem(
                current ? QStringLiteral("%1（当前）").arg(databaseName) : databaseName,
                QStringLiteral("database"), QStringLiteral("database"), databaseName);
            if (current) {
                QFont font = database->font();
                font.setBold(true);
                database->setFont(font);
                database->appendRow(schemaItem(QStringLiteral("Schemas"), QStringLiteral("schemas"),
                                               QStringLiteral("boxes"), {}, {}, true));
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
    const QString title = QStringLiteral("Query %1").arg(queryNumber_++);
    page->setProperty("tabTitle", title);
    const int index = queryTabs_->addTab(page, toolbarIcon(QStringLiteral("file-code")), title);
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
    if (!postgres_.isConnected()) {
        connectSelectedConnection();
        return;
    }
    populateSchema();
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
    const QString connectionId = source.data(ConnectionIdRole).toString();
    if (type == QStringLiteral("connection") && !connectionId.isEmpty()) {
        if (const SavedConnection *connection = savedConnection(connectionId)) {
            updateInspector(*connection, postgres_.isConnected()
                                             && connectionId == activeConnectionId_);
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
    const QString connectionId = source.data(ConnectionIdRole).toString();
    QString error;

    if (type == QStringLiteral("connection")) {
        if (connectionId.isEmpty()) {
            showPostgresConnectionDialog();
        } else if (postgres_.isConnected()
                   && connectionId == activeConnectionId_) {
            schemaTree_->setExpanded(proxyIndex,
                                     !schemaTree_->isExpanded(proxyIndex));
        } else {
            connectSavedConnection(connectionId);
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
        updateConnectionUi();
        populateSchema();
        statusBar()->showMessage(QStringLiteral("当前数据库已切换为 %1").arg(name), 5000);
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
        page->setTableContext(schema, name, table.primaryKeys());
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
    if (type == QStringLiteral("connection")) {
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
    if (type != QStringLiteral("connection"))
        description_->setText(QStringLiteral("—"));
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
    const bool connected = postgres_.isConnected();
    reconnectAction_->setEnabled(connected);
    disconnectAction_->setEnabled(connected);
    if (!connected) {
        const SavedConnection *active = savedConnection(activeConnectionId_);
        const QString label = active
            ? QStringLiteral("%1 · 未连接").arg(active->config.displayName())
            : savedConnections_.isEmpty()
                  ? QStringLiteral("PostgreSQL · 未连接")
                  : QStringLiteral("%1 个已保存连接").arg(savedConnections_.size());
        connectionContext_->addItem(toolbarIcon(QStringLiteral("postgresql")), label);
        schemaContext_->addItem(toolbarIcon(QStringLiteral("boxes")), QStringLiteral("无活动数据库"));
        return;
    }

    const QString label = QStringLiteral("%1@%2 / %3")
                              .arg(postgres_.currentUser(), postgres_.config().host,
                                   postgres_.config().database);
    connectionContext_->addItem(toolbarIcon(QStringLiteral("postgresql")), label);
    schemaContext_->addItem(toolbarIcon(QStringLiteral("boxes")),
                            QStringLiteral("public @ %1").arg(postgres_.config().database));
}

void MainWindow::showPostgresConnectionDialog()
{
    editConnection(QString{});
}

void MainWindow::editSelectedConnection()
{
    QString connectionId = selectedConnectionId();
    if (connectionId.isEmpty())
        connectionId = activeConnectionId_;
    if (connectionId.isEmpty() && !savedConnections_.isEmpty())
        connectionId = savedConnections_.constFirst().id;
    editConnection(connectionId);
}

void MainWindow::editConnection(const QString &connectionId, const QString &notice)
{
    SavedConnection draft;
    if (const SavedConnection *existing = savedConnection(connectionId))
        draft = *existing;

    if (!notice.isEmpty())
        QMessageBox::warning(this, QStringLiteral("需要更新连接信息"), notice);

    PostgresConnectionDialog dialog(this);
    if (!draft.id.isEmpty())
        dialog.setConfig(draft.config);

    while (dialog.exec() == QDialog::Accepted) {
        const PostgresConnectionConfig candidate = dialog.config();
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
            dialog.setConfig(candidate);
            continue;
        }

        draft.config = candidate;
        QSettings settings;
        QString saveError;
        const bool saved = ConnectionStore::upsert(
            settings, *credentialStore_, &draft, &saveError);
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

        updateConnectionUi();
        populateSchema();
        if (const SavedConnection *active = savedConnection(activeConnectionId_))
            updateInspector(*active, true);
        else
            updateInspector(QStringLiteral("PostgreSQL"), QStringLiteral("connection"));
        statusBar()->showMessage(
            saved
                ? QStringLiteral("已连接并保存 %1").arg(postgres_.config().displayName())
                : QStringLiteral("已连接 %1（未保存）").arg(postgres_.config().displayName()),
            6000);
        return;
    }

    updateConnectionUi();
    populateSchema();
    if (const SavedConnection *active = savedConnection(activeConnectionId_))
        updateInspector(*active, postgres_.isConnected());
    else
        updateInspector(QStringLiteral("PostgreSQL"), QStringLiteral("connection"));
}

void MainWindow::connectSelectedConnection()
{
    QString connectionId = selectedConnectionId();
    if (connectionId.isEmpty())
        connectionId = activeConnectionId_;
    if (connectionId.isEmpty() && !savedConnections_.isEmpty())
        connectionId = savedConnections_.constFirst().id;
    if (connectionId.isEmpty()) {
        showPostgresConnectionDialog();
        return;
    }
    connectSavedConnection(connectionId);
}

void MainWindow::connectSavedConnection(const QString &connectionId)
{
    const SavedConnection *stored = savedConnection(connectionId);
    if (!stored)
        return;
    if (postgres_.isConnected() && connectionId == activeConnectionId_) {
        statusBar()->showMessage(QStringLiteral("该数据库已经连接"), 3000);
        return;
    }
    if (!stored->hasStoredPassword) {
        editConnection(
            connectionId,
            QStringLiteral("没有找到此连接的已保存密码，请重新输入连接信息。"));
        return;
    }

    const PostgresConnectionConfig config = stored->config;
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
                .arg(error.isEmpty() ? QStringLiteral("未知数据库错误") : error));
        return;
    }

    activeConnectionId_ = connectionId;
    updateConnectionUi();
    populateSchema();
    if (const SavedConnection *active = savedConnection(activeConnectionId_))
        updateInspector(*active, true);
    statusBar()->showMessage(
        QStringLiteral("已使用保存的凭据连接 %1").arg(config.displayName()), 5000);
}

void MainWindow::removeSelectedConnection()
{
    const QString connectionId = selectedConnectionId();
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
    updateConnectionUi();
    populateSchema();
    if (const SavedConnection *active = savedConnection(activeConnectionId_))
        updateInspector(*active, postgres_.isConnected());
    else
        updateInspector(QStringLiteral("PostgreSQL"), QStringLiteral("connection"));
    statusBar()->showMessage(QStringLiteral("已删除 %1").arg(displayName), 5000);
}

QString MainWindow::selectedConnectionId() const
{
    if (!schemaTree_ || !schemaTree_->currentIndex().isValid())
        return {};
    QModelIndex source = schemaProxy_->mapToSource(schemaTree_->currentIndex());
    while (source.isValid()) {
        const QString connectionId = source.data(ConnectionIdRole).toString();
        if (!connectionId.isEmpty())
            return connectionId;
        source = source.parent();
    }
    return {};
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

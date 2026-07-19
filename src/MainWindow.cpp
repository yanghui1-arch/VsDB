#include "MainWindow.h"

#include "SqlEditor.h"
#include "UiComponents.h"
#include "WorkbenchModels.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QSvgRenderer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace vsdb {

namespace {
constexpr int TypeRole = Qt::UserRole + 1;

const QString kInitialSql = QStringLiteral(
    "SELECT\n"
    "    u.id,\n"
    "    u.email,\n"
    "    u.created_at,\n"
    "    o.id AS order_id,\n"
    "    o.total,\n"
    "    o.status\n"
    "FROM public.users u\n"
    "LEFT JOIN public.orders o ON o.user_id = u.id\n"
    "WHERE u.created_at >= '2026-07-01'\n"
    "ORDER BY u.created_at DESC\n"
    "LIMIT 100;");

QToolButton *plainButton(QWidget *parent, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("plainButton"));
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(false);
    return button;
}

QIcon toolbarIcon(const QString &name);

QStandardItem *schemaItem(const QString &text, const QString &type, const QString &iconName)
{
    auto *item = new QStandardItem(text);
    if (!iconName.isEmpty())
        item->setIcon(toolbarIcon(iconName));
    item->setData(type, TypeRole);
    item->setData(text, Qt::UserRole);
    item->setEditable(false);
    return item;
}

QIcon toolbarIcon(const QString &name)
{
    QSvgRenderer renderer(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    QIcon icon;
    for (const qreal dpr : {1.0, 2.0}) {
        const int pixels = qRound(16 * dpr);
        QPixmap pixmap(pixels, pixels);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(dpr);
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(0, 0, 16, 16));
        icon.addPixmap(pixmap);
    }
    return icon;
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

        // The final indentation slot belongs to the branch control. Keeping
        // the chevron inside it prevents the indicator from crossing into the
        // item rectangle painted with the selection/hover background.
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
                ? QColor(QStringLiteral("#E7E8ED"))
                : QColor(QStringLiteral("#A3A6B2")));
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

class ResultRowHeaderView final : public QHeaderView
{
public:
    explicit ResultRowHeaderView(QTableView *table)
        : QHeaderView(Qt::Vertical, table), table_(table)
    {
        setSectionsClickable(true);
        setHighlightSections(true);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        if (!rect.isValid())
            return;

        const bool selected = table_->selectionModel()
            && table_->selectionModel()->isRowSelected(logicalIndex, QModelIndex{});
        painter->save();
        painter->fillRect(rect, selected ? QColor(QStringLiteral("#173A5E"))
                                         : QColor(QStringLiteral("#24252E")));
        painter->setPen(QColor(QStringLiteral("#30323C")));
        painter->drawLine(rect.topRight(), rect.bottomRight());
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        painter->setPen(selected ? QColor(QStringLiteral("#E7E8ED"))
                                 : QColor(QStringLiteral("#B7BAC5")));
        painter->drawText(rect.adjusted(10, 0, -6, 0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString());
        painter->restore();
    }

private:
    QTableView *table_ = nullptr;
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

QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted"));
    return label;
}
}

class QueryPage final : public QWidget
{
public:
    explicit QueryPage(const QString &sql, QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("queryPage"));
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        splitter = new QSplitter(Qt::Vertical, this);
        editor = new SqlEditor(splitter);
        editor->setPlainText(sql);
        splitter->addWidget(editor);

        auto *results = new QWidget(splitter);
        auto *resultsLayout = new QVBoxLayout(results);
        resultsLayout->setContentsMargins(10, 0, 10, 8);
        resultsLayout->setSpacing(0);

        auto *resultTabs = new QTabBar(results);
        resultTabs->setDrawBase(false);
        resultTabs->addTab(QStringLiteral("Results 1"));
        resultTabs->addTab(QStringLiteral("Messages"));
        resultTabs->setCurrentIndex(0);
        resultTabs->setExpanding(false);
        resultsLayout->addWidget(resultTabs);

        auto *tools = new QWidget(results);
        tools->setFixedHeight(50);
        auto *toolLayout = new QHBoxLayout(tools);
        toolLayout->setContentsMargins(0, 7, 0, 7);
        toolLayout->setSpacing(7);
        auto *gridButton = plainButton(tools, QString{}, QStringLiteral("Grid view"));
        gridButton->setIcon(toolbarIcon(QStringLiteral("layout-grid")));
        auto *recordButton = plainButton(tools, QString{}, QStringLiteral("Record view"));
        recordButton->setIcon(toolbarIcon(QStringLiteral("rows-3")));
        toolLayout->addWidget(gridButton);
        toolLayout->addWidget(recordButton);
        auto *exportButton = new QPushButton(toolbarIcon(QStringLiteral("upload")), QStringLiteral("Export"), tools);
        exportButton->setToolTip(QStringLiteral("Export visible results"));
        toolLayout->addSpacing(6);
        toolLayout->addWidget(exportButton);
        auto *copyButton = new QPushButton(toolbarIcon(QStringLiteral("copy")), QStringLiteral("Copy"), tools);
        toolLayout->addWidget(copyButton);
        toolLayout->addSpacing(6);
        commitButton = new QPushButton(toolbarIcon(QStringLiteral("save")), QStringLiteral("Commit"), tools);
        commitButton->setToolTip(QStringLiteral("Commit all pending cell edits in one transaction"));
        commitButton->setEnabled(false);
        toolLayout->addWidget(commitButton);
        rollbackButton = new QPushButton(toolbarIcon(QStringLiteral("undo-2")), QStringLiteral("Rollback"), tools);
        rollbackButton->setToolTip(QStringLiteral("Discard all pending cell edits"));
        rollbackButton->setEnabled(false);
        toolLayout->addWidget(rollbackButton);
        toolLayout->addStretch();
        rowLimit = new ModernComboBox(tools);
        rowLimit->setObjectName(QStringLiteral("rowLimitCombo"));
        rowLimit->addItems({QStringLiteral("50 rows"), QStringLiteral("100 rows"), QStringLiteral("500 rows")});
        rowLimit->setCurrentIndex(1);
        rowLimit->setFixedWidth(112);
        toolLayout->addWidget(rowLimit);
        auto *settingsButton = plainButton(tools, QString{}, QStringLiteral("Result settings"));
        settingsButton->setIcon(toolbarIcon(QStringLiteral("settings")));
        toolLayout->addWidget(settingsButton);
        resultsLayout->addWidget(tools);

        model = new ResultTableModel(this);
        table = new QTableView(results);
        table->setHorizontalHeader(new TwoLineHeaderView(Qt::Horizontal, table));
        table->setVerticalHeader(new ResultRowHeaderView(table));
        table->setModel(model);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectItems);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked);
        table->setSortingEnabled(false);
        table->verticalHeader()->setDefaultSectionSize(37);
        table->verticalHeader()->setMinimumWidth(38);
        table->horizontalHeader()->setFixedHeight(49);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setColumnWidth(0, 70);
        table->setColumnWidth(1, 175);
        table->setColumnWidth(2, 160);
        table->setColumnWidth(3, 90);
        table->setColumnWidth(4, 90);
        table->setColumnWidth(5, 130);
        resultsLayout->addWidget(table, 1);

        summary = new QLabel(QStringLiteral("Ready · 100 rows available"), results);
        summary->setObjectName(QStringLiteral("muted"));
        summary->setContentsMargins(4, 5, 0, 0);
        resultsLayout->addWidget(summary);

        splitter->addWidget(results);
        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 3);
        splitter->setSizes({320, 450});
        root->addWidget(splitter);

        connect(table->verticalHeader(), &QHeaderView::sectionClicked, this, [this](int row) {
            table->setCurrentIndex(model->index(row, 0));
            const QItemSelection entireRow(model->index(row, 0),
                                           model->index(row, model->columnCount() - 1));
            table->selectionModel()->select(entireRow, QItemSelectionModel::ClearAndSelect);
        });
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged,
                table->verticalHeader()->viewport(), qOverload<>(&QWidget::update));
        connect(model, &QAbstractItemModel::dataChanged, this, [this] {
            updateTransactionState();
        });
        connect(commitButton, &QPushButton::clicked, this, [this] {
            const int changes = model->pendingChangeCount();
            if (changes <= 0)
                return;
            model->commitPendingChanges();
            summary->setText(QStringLiteral("Committed %1 edit%2 in one transaction")
                                 .arg(changes)
                                 .arg(changes == 1 ? QString{} : QStringLiteral("s")));
        });
        connect(rollbackButton, &QPushButton::clicked, this, [this] {
            const int changes = model->pendingChangeCount();
            if (changes <= 0)
                return;
            model->rollbackPendingChanges();
            summary->setText(QStringLiteral("Rolled back %1 pending edit%2")
                                 .arg(changes)
                                 .arg(changes == 1 ? QString{} : QStringLiteral("s")));
        });

        connect(rowLimit, &QComboBox::currentIndexChanged, this, [this](int index) {
            const int rows[] = {50, 100, 500};
            model->setVisibleRows(rows[index]);
            updateTransactionState();
        });
        connect(copyButton, &QPushButton::clicked, this, [this] {
            const QModelIndexList selected = table->selectionModel()->selectedIndexes();
            if (selected.isEmpty())
                return;
            QString text;
            int lastRow = selected.constFirst().row();
            for (const QModelIndex &index : selected) {
                if (!text.isEmpty())
                    text += index.row() == lastRow ? QLatin1Char('\t') : QLatin1Char('\n');
                text += index.data().toString();
                lastRow = index.row();
            }
            QApplication::clipboard()->setText(text);
        });
        connect(exportButton, &QPushButton::clicked, this, [this] {
            summary->setText(QStringLiteral("Export is available when a database session is connected"));
        });
        connect(resultTabs, &QTabBar::currentChanged, this, [this](int index) {
            table->setVisible(index == 0);
            if (index == 0)
                updateTransactionState();
            else
                summary->setText(QStringLiteral("Query completed without messages"));
        });
    }

    void updateTransactionState()
    {
        const int changes = model->pendingChangeCount();
        commitButton->setEnabled(changes > 0);
        rollbackButton->setEnabled(changes > 0);
        commitButton->setText(changes > 0
            ? QStringLiteral("Commit (%1)").arg(changes)
            : QStringLiteral("Commit"));
        summary->setText(changes > 0
            ? QStringLiteral("%1 pending edit%2 · Commit applies all edits in one transaction")
                  .arg(changes)
                  .arg(changes == 1 ? QString{} : QStringLiteral("s"))
            : QStringLiteral("Ready · %1 rows available").arg(model->rowCount()));
    }

    SqlEditor *editor = nullptr;
    ResultTableModel *model = nullptr;
    QTableView *table = nullptr;
    QSplitter *splitter = nullptr;
    QLabel *summary = nullptr;
    QComboBox *rowLimit = nullptr;
    QPushButton *commitButton = nullptr;
    QPushButton *rollbackButton = nullptr;
    QElapsedTimer elapsed;
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("VsDB — Fast database workbench"));
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

    populateSchema();
    installActions();
    addQuery(kInitialSql);
    updateInspector(QStringLiteral("users"), QStringLiteral("Table"));

    executionTimer_ = new QTimer(this);
    executionTimer_->setSingleShot(true);
    connect(executionTimer_, &QTimer::timeout, this, [this] {
        QueryPage *page = currentQuery();
        if (!page)
            return;
        const qint64 ms = qMax<qint64>(16, page->elapsed.elapsed());
        page->summary->setText(QStringLiteral("Query completed · %1 rows · %2 ms").arg(page->model->rowCount()).arg(ms));
    });

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    mainSplitter_->restoreState(settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
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

    auto addAction = [toolbar](const QString &icon, const QString &text,
                               const QString &tip) {
        QAction *action = toolbar->addAction(toolbarIcon(icon), text);
        action->setToolTip(tip);
        return action;
    };

    auto *newConnection = new QToolButton(toolbar);
    newConnection->setIcon(toolbarIcon(QStringLiteral("database-zap")));
    newConnection->setToolTip(QStringLiteral("New connection"));
    newConnection->setPopupMode(QToolButton::MenuButtonPopup);
    auto *connectionMenu = new QMenu(newConnection);
    connectionMenu->addAction(QStringLiteral("PostgreSQL"));
    connectionMenu->addAction(QStringLiteral("MySQL"));
    connectionMenu->addAction(QStringLiteral("Redis"));
    newConnection->setMenu(connectionMenu);
    toolbar->addWidget(newConnection);

    addAction(QStringLiteral("cloud-cog"), QStringLiteral("Manage connections"),
              QStringLiteral("Manage connections"));
    addAction(QStringLiteral("folder-open"), QStringLiteral("Open project"),
              QStringLiteral("Open database project"));
    toolbar->addSeparator();

    addAction(QStringLiteral("plug"), QStringLiteral("Connect"),
              QStringLiteral("Connect"));
    QAction *reconnect = addAction(QStringLiteral("refresh-cw"), QStringLiteral("Reconnect"),
                                   QStringLiteral("Reconnect and refresh"));
    connect(reconnect, &QAction::triggered, this, &MainWindow::refreshSchema);
    addAction(QStringLiteral("unplug"), QStringLiteral("Disconnect"),
              QStringLiteral("Disconnect"));
    toolbar->addSeparator();

    auto *sqlMenuButton = new QToolButton(toolbar);
    sqlMenuButton->setIcon(toolbarIcon(QStringLiteral("file-code")));
    sqlMenuButton->setText(QStringLiteral("SQL"));
    sqlMenuButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sqlMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto *sqlMenu = new QMenu(sqlMenuButton);
    QAction *runSql = sqlMenu->addAction(QStringLiteral("Run SQL    Ctrl+Enter"));
    connect(runSql, &QAction::triggered, this, &MainWindow::executeQuery);
    QAction *newSql = sqlMenu->addAction(QStringLiteral("New SQL script    Ctrl+T"));
    connect(newSql, &QAction::triggered, this, [this] { addQuery(); });
    sqlMenuButton->setMenu(sqlMenu);
    toolbar->addWidget(sqlMenuButton);
    toolbar->addSeparator();

    QAction *newScript = addAction(QStringLiteral("file-plus"), QStringLiteral("New SQL script"),
                                   QStringLiteral("New SQL script (Ctrl+T)"));
    connect(newScript, &QAction::triggered, this, [this] { addQuery(); });
    QAction *commit = addAction(QStringLiteral("save"), QStringLiteral("Commit"),
                                QStringLiteral("Commit transaction"));
    commit->setEnabled(false);
    QAction *rollback = addAction(QStringLiteral("undo-2"), QStringLiteral("Rollback"),
                                  QStringLiteral("Rollback transaction"));
    rollback->setEnabled(false);
    toolbar->addSeparator();

    auto *historyButton = new QToolButton(toolbar);
    historyButton->setIcon(toolbarIcon(QStringLiteral("history")));
    historyButton->setToolTip(QStringLiteral("Query history"));
    historyButton->setPopupMode(QToolButton::InstantPopup);
    auto *historyMenu = new QMenu(historyButton);
    historyMenu->addAction(QStringLiteral("No query history yet"))->setEnabled(false);
    historyButton->setMenu(historyMenu);
    toolbar->addWidget(historyButton);
    toolbar->addSeparator();

    auto *connectionContext = new ModernComboBox(toolbar);
    connectionContext->setObjectName(QStringLiteral("toolbarCombo"));
    connectionContext->addItem(toolbarIcon(QStringLiteral("postgresql")), QStringLiteral("PostgreSQL 15"));
    connectionContext->addItem(toolbarIcon(QStringLiteral("mysql")), QStringLiteral("MySQL 8.0"));
    connectionContext->addItem(toolbarIcon(QStringLiteral("redis")), QStringLiteral("Redis 7.2"));
    connectionContext->setMinimumWidth(138);
    toolbar->addWidget(connectionContext);

    auto *schemaContext = new ModernComboBox(toolbar);
    schemaContext->setObjectName(QStringLiteral("toolbarCombo"));
    schemaContext->addItem(toolbarIcon(QStringLiteral("table-2")), QStringLiteral("public @ analytics"));
    schemaContext->addItem(toolbarIcon(QStringLiteral("table-2")), QStringLiteral("analytics @ analytics"));
    schemaContext->addItem(toolbarIcon(QStringLiteral("table-2")), QStringLiteral("staging @ analytics"));
    schemaContext->setMinimumWidth(175);
    toolbar->addWidget(schemaContext);
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

    auto *footer = new QWidget(pane);
    footer->setFixedHeight(43);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(9, 3, 9, 3);
    footerLayout->addWidget(plainButton(footer, QStringLiteral("＋"), QStringLiteral("Add connection")));
    footerLayout->addWidget(plainButton(footer, QStringLiteral("⇧"), QStringLiteral("Import connections")));
    footerLayout->addWidget(plainButton(footer, QStringLiteral("⌫"), QStringLiteral("Remove connection")));
    footerLayout->addStretch();
    layout->addWidget(footer);

    connect(schemaTree_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::handleSchemaSelection);
    connect(schemaTree_, &QTreeView::doubleClicked, this, &MainWindow::activateSchemaItem);
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
    queryTabs_->setTabsClosable(false);
    queryTabs_->setMovable(true);
    queryTabs_->tabBar()->setObjectName(QStringLiteral("queryTabBar"));
    queryTabs_->tabBar()->setDrawBase(false);
    queryTabs_->tabBar()->setExpanding(false);
    queryTabs_->tabBar()->setIconSize(QSize(14, 14));
    queryTabs_->tabBar()->setFixedHeight(32);
    queryTabs_->tabBar()->setElideMode(Qt::ElideRight);
    queryTabs_->tabBar()->setUsesScrollButtons(false);
    auto *add = plainButton(queryTabs_, QStringLiteral("＋"), QStringLiteral("New query (Ctrl+T)"));
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
    objectIcon_->setPixmap(toolbarIcon(QStringLiteral("table-2")).pixmap(QSize(20, 20)));
    objectIcon_->setAlignment(Qt::AlignCenter);
    objectIcon_->setFixedSize(46, 46);
    headingLayout->addWidget(objectIcon_);
    auto *names = new QWidget(heading);
    auto *namesLayout = new QVBoxLayout(names);
    namesLayout->setContentsMargins(0, 0, 0, 0);
    namesLayout->setSpacing(1);
    objectName_ = new QLabel(QStringLiteral("users"), names);
    objectName_->setObjectName(QStringLiteral("paneHeading"));
    objectType_ = mutedLabel(QStringLiteral("Table"), names);
    namesLayout->addWidget(objectName_);
    namesLayout->addWidget(objectType_);
    headingLayout->addWidget(names, 1);
    bodyLayout->addWidget(heading);
    objectPath_ = mutedLabel(QStringLiteral("public.users"), body);
    bodyLayout->addWidget(objectPath_);

    auto *details = new QWidget(body);
    auto *form = new QFormLayout(details);
    form->setContentsMargins(0, 3, 0, 3);
    form->setHorizontalSpacing(28);
    form->setVerticalSpacing(9);
    rowEstimate_ = new QLabel(QStringLiteral("~ 10,248"), details);
    form->addRow(mutedLabel(QStringLiteral("Row estimate"), details), rowEstimate_);
    form->addRow(mutedLabel(QStringLiteral("Size"), details), new QLabel(QStringLiteral("2.1 MB"), details));
    form->addRow(mutedLabel(QStringLiteral("Description"), details), new QLabel(QStringLiteral("Application users"), details));
    bodyLayout->addWidget(details);

    auto *metadataTabs = new QTabWidget(body);
    metadataTabs->tabBar()->setObjectName(QStringLiteral("metadataTabBar"));
    metadataTabs->tabBar()->setDrawBase(false);
    metadataTabs->tabBar()->setExpanding(true);
    metadataTabs->tabBar()->setUsesScrollButtons(false);
    metadataTabs->tabBar()->setElideMode(Qt::ElideRight);
    columnModel_ = new QStandardItemModel(0, 4, metadataTabs);
    columnModel_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"), QStringLiteral("Nullable"), QStringLiteral("Default")});
    auto *columns = new QTableView(metadataTabs);
    columns->setModel(columnModel_);
    columns->verticalHeader()->hide();
    columns->setShowGrid(false);
    columns->setSelectionBehavior(QAbstractItemView::SelectRows);
    columns->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    columns->setColumnWidth(1, 90);
    columns->setColumnWidth(2, 70);
    metadataTabs->addTab(columns, QStringLiteral("Columns"));

    indexModel_ = new QStandardItemModel(0, 3, metadataTabs);
    indexModel_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Columns"), QStringLiteral("Type")});
    auto *indexes = new QTableView(metadataTabs);
    indexes->setModel(indexModel_);
    indexes->verticalHeader()->hide();
    indexes->setShowGrid(false);
    indexes->setSelectionBehavior(QAbstractItemView::SelectRows);
    indexes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    indexes->setColumnWidth(1, 72);
    indexes->setColumnWidth(2, 76);
    metadataTabs->addTab(indexes, QStringLiteral("Indexes"));

    foreignKeyModel_ = new QStandardItemModel(0, 3, metadataTabs);
    foreignKeyModel_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Column"), QStringLiteral("References")});
    auto *foreignKeys = new QTableView(metadataTabs);
    foreignKeys->setModel(foreignKeyModel_);
    foreignKeys->verticalHeader()->hide();
    foreignKeys->setShowGrid(false);
    foreignKeys->setSelectionBehavior(QAbstractItemView::SelectRows);
    foreignKeys->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    foreignKeys->setColumnWidth(1, 86);
    foreignKeys->setColumnWidth(2, 106);
    metadataTabs->addTab(foreignKeys, QStringLiteral("Foreign Keys"));
    bodyLayout->addWidget(metadataTabs, 2);
    auto *pages = new QStackedWidget(pane);
    pages->addWidget(body);
    auto *historyPage = new QWidget(pages);
    historyPage->setObjectName(QStringLiteral("sidePane"));
    pages->addWidget(historyPage);
    layout->addWidget(pages, 1);

    connect(topTabs, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);
    return pane;
}

void MainWindow::populateSchema()
{
    schemaModel_->clear();
    auto *postgres = schemaItem(QStringLiteral("PostgreSQL 15"), QStringLiteral("connection"), QString{});
    postgres->setIcon(toolbarIcon(QStringLiteral("postgresql")));
    auto *databases = schemaItem(QStringLiteral("Databases"), QStringLiteral("group"), QStringLiteral("database"));
    databases->appendRow(schemaItem(QStringLiteral("postgres"), QStringLiteral("database"), QStringLiteral("database")));
    databases->appendRow(schemaItem(QStringLiteral("analytics"), QStringLiteral("database"), QStringLiteral("database")));
    databases->appendRow(schemaItem(QStringLiteral("template1"), QStringLiteral("database"), QStringLiteral("database")));
    postgres->appendRow(databases);

    auto *schemas = schemaItem(QStringLiteral("Schemas"), QStringLiteral("group"), QStringLiteral("boxes"));
    auto *publicSchema = schemaItem(QStringLiteral("public"), QStringLiteral("schema"), QStringLiteral("boxes"));
    auto *tables = schemaItem(QStringLiteral("Tables"), QStringLiteral("group"), QStringLiteral("table-2"));
    for (const QString &name : {QStringLiteral("users"), QStringLiteral("orders"), QStringLiteral("products"), QStringLiteral("events")})
        tables->appendRow(schemaItem(name, QStringLiteral("Table"), QStringLiteral("table-2")));
    tables->appendRow(schemaItem(QStringLiteral("…"), QStringLiteral("more"), QStringLiteral("")));
    publicSchema->appendRow(tables);
    publicSchema->appendRow(schemaItem(QStringLiteral("Views"), QStringLiteral("group"), QStringLiteral("eye")));
    publicSchema->appendRow(schemaItem(QStringLiteral("Indexes"), QStringLiteral("group"), QStringLiteral("list-tree")));
    publicSchema->appendRow(schemaItem(QStringLiteral("Functions"), QStringLiteral("group"), QStringLiteral("square-function")));
    publicSchema->appendRow(schemaItem(QStringLiteral("Sequences"), QStringLiteral("group"), QStringLiteral("list-ordered")));
    schemas->appendRow(publicSchema);
    schemas->appendRow(schemaItem(QStringLiteral("analytics"), QStringLiteral("schema"), QStringLiteral("boxes")));
    schemas->appendRow(schemaItem(QStringLiteral("staging"), QStringLiteral("schema"), QStringLiteral("boxes")));
    schemas->appendRow(schemaItem(QStringLiteral("information_schema"), QStringLiteral("schema"), QStringLiteral("boxes")));
    postgres->appendRow(schemas);
    schemaModel_->appendRow(postgres);

    auto *mysql = schemaItem(QStringLiteral("MySQL 8.0"), QStringLiteral("connection"), QString{});
    mysql->setIcon(toolbarIcon(QStringLiteral("mysql")));
    schemaModel_->appendRow(mysql);
    auto *redis = schemaItem(QStringLiteral("Redis 7.2"), QStringLiteral("connection"), QString{});
    redis->setIcon(toolbarIcon(QStringLiteral("redis")));
    auto *db0 = schemaItem(QStringLiteral("db0"), QStringLiteral("database"), QStringLiteral("database"));
    auto *cache = schemaItem(QStringLiteral("cache"), QStringLiteral("group"), QStringLiteral("folder-open"));
    for (const QString &key : {QStringLiteral("session:1a2b"), QStringLiteral("session:3f9c"), QStringLiteral("user:1001")})
        cache->appendRow(schemaItem(key, QStringLiteral("Redis key"), QStringLiteral("key-round")));
    db0->appendRow(cache);
    redis->appendRow(db0);
    redis->appendRow(schemaItem(QStringLiteral("db1"), QStringLiteral("database"), QStringLiteral("database")));
    schemaModel_->appendRow(redis);

    schemaTree_->expandToDepth(4);
    const QModelIndexList users = schemaModel_->match(
        schemaModel_->index(0, 0), Qt::UserRole, QStringLiteral("users"), 1,
        Qt::MatchExactly | Qt::MatchRecursive);
    if (!users.isEmpty())
        schemaTree_->setCurrentIndex(schemaProxy_->mapFromSource(users.constFirst()));
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
    auto *page = new QueryPage(sql.isEmpty() ? QStringLiteral("SELECT *\nFROM public.users\nLIMIT 100;") : sql, queryTabs_);
    const QString title = QStringLiteral("Query %1").arg(queryNumber_++);
    page->setProperty("tabTitle", title);
    const int index = queryTabs_->addTab(page, toolbarIcon(QStringLiteral("file-code")), title);
    auto *closeButton = new QToolButton(queryTabs_->tabBar());
    closeButton->setObjectName(QStringLiteral("tabCloseButton"));
    closeButton->setIcon(toolbarIcon(QStringLiteral("x")));
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(18, 18);
    closeButton->setToolTip(QStringLiteral("Close query (Ctrl+W)"));
    queryTabs_->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);
    connect(closeButton, &QToolButton::clicked, this, [this, page] {
        closeQuery(queryTabs_->indexOf(page));
    });
    queryTabs_->setCurrentIndex(index);
    page->editor->document()->setModified(false);
    connect(page->editor->document(), &QTextDocument::modificationChanged, this, [this, page](bool modified) {
        const int tabIndex = queryTabs_->indexOf(page);
        if (tabIndex < 0)
            return;
        const QString baseTitle = page->property("tabTitle").toString();
        queryTabs_->setTabText(tabIndex, modified ? QStringLiteral("* %1").arg(baseTitle) : baseTitle);
    });
    page->editor->setFocus();
}

QueryPage *MainWindow::currentQuery() const
{
    return static_cast<QueryPage *>(queryTabs_->currentWidget());
}

void MainWindow::executeQuery()
{
    QueryPage *page = currentQuery();
    if (!page || executionTimer_->isActive())
        return;
    page->elapsed.start();
    page->summary->setText(QStringLiteral("Running query…"));
    executionTimer_->start(90);
}

void MainWindow::stopQuery()
{
    if (!executionTimer_ || !executionTimer_->isActive())
        return;
    executionTimer_->stop();
    if (QueryPage *page = currentQuery())
        page->summary->setText(QStringLiteral("Query cancelled"));
}

void MainWindow::refreshSchema()
{
    QTimer::singleShot(120, this, [this] {
        schemaTree_->expandToDepth(4);
    });
}

void MainWindow::closeQuery(int index)
{
    if (index < 0)
        return;
    QWidget *page = queryTabs_->widget(index);
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
    updateInspector(source.data(Qt::UserRole).toString(), source.data(TypeRole).toString());
}

void MainWindow::activateSchemaItem(const QModelIndex &proxyIndex)
{
    const QModelIndex source = schemaProxy_->mapToSource(proxyIndex);
    if (source.data(TypeRole).toString() != QStringLiteral("Table"))
        return;
    const QString name = source.data(Qt::UserRole).toString();
    addQuery(QStringLiteral("SELECT *\nFROM public.%1\nLIMIT 100;").arg(name));
}

void MainWindow::updateInspector(const QString &name, const QString &type)
{
    const bool isTable = type == QStringLiteral("Table");
    QString iconName = QStringLiteral("boxes");
    if (isTable)
        iconName = QStringLiteral("table-2");
    else if (type == QStringLiteral("database"))
        iconName = QStringLiteral("database");
    else if (type == QStringLiteral("schema"))
        iconName = QStringLiteral("boxes");
    else if (type == QStringLiteral("Redis key"))
        iconName = QStringLiteral("key-round");
    else if (type == QStringLiteral("connection")) {
        if (name.startsWith(QStringLiteral("PostgreSQL")))
            iconName = QStringLiteral("postgresql");
        else if (name.startsWith(QStringLiteral("MySQL")))
            iconName = QStringLiteral("mysql");
        else if (name.startsWith(QStringLiteral("Redis")))
            iconName = QStringLiteral("redis");
    } else if (type == QStringLiteral("group")) {
        if (name == QStringLiteral("Databases"))
            iconName = QStringLiteral("database");
        else if (name == QStringLiteral("Tables"))
            iconName = QStringLiteral("table-2");
        else if (name == QStringLiteral("Views"))
            iconName = QStringLiteral("eye");
        else if (name == QStringLiteral("Indexes"))
            iconName = QStringLiteral("list-tree");
        else if (name == QStringLiteral("Functions"))
            iconName = QStringLiteral("square-function");
        else if (name == QStringLiteral("Sequences"))
            iconName = QStringLiteral("list-ordered");
        else if (name == QStringLiteral("cache"))
            iconName = QStringLiteral("folder-open");
    }
    objectIcon_->setPixmap(toolbarIcon(iconName).pixmap(QSize(20, 20)));
    objectName_->setText(name.isEmpty() ? QStringLiteral("users") : name);
    objectType_->setText(type.isEmpty() ? QStringLiteral("Table") : type);
    objectPath_->setText(isTable ? QStringLiteral("public.%1").arg(name) : QStringLiteral("analytics / public"));
    rowEstimate_->setText(isTable ? QStringLiteral("~ 10,248") : QStringLiteral("—"));

    columnModel_->removeRows(0, columnModel_->rowCount());
    indexModel_->removeRows(0, indexModel_->rowCount());
    foreignKeyModel_->removeRows(0, foreignKeyModel_->rowCount());
    const QList<QStringList> columns{
        {QStringLiteral("id"), QStringLiteral("bigint"), QStringLiteral("NO"), QStringLiteral("nextval(…)")},
        {QStringLiteral("email"), QStringLiteral("text"), QStringLiteral("NO"), QStringLiteral("—")},
        {QStringLiteral("password_hash"), QStringLiteral("text"), QStringLiteral("NO"), QStringLiteral("—")},
        {QStringLiteral("organization_id"), QStringLiteral("uuid"), QStringLiteral("NO"), QStringLiteral("—")},
        {QStringLiteral("created_at"), QStringLiteral("timestamptz"), QStringLiteral("NO"), QStringLiteral("now()")},
        {QStringLiteral("updated_at"), QStringLiteral("timestamptz"), QStringLiteral("YES"), QStringLiteral("now()")},
        {QStringLiteral("is_active"), QStringLiteral("boolean"), QStringLiteral("NO"), QStringLiteral("true")}};
    if (isTable) {
        for (const QStringList &row : columns) {
            QList<QStandardItem *> items;
            for (const QString &cell : row) {
                auto *item = new QStandardItem(cell);
                item->setEditable(false);
                items.append(item);
            }
            if (row.constFirst() == QStringLiteral("id"))
                items.constFirst()->setIcon(toolbarIcon(QStringLiteral("key-round")));
            columnModel_->appendRow(items);
        }

        const QList<QStringList> indexes{
            {QStringLiteral("users_pkey"), QStringLiteral("id"), QStringLiteral("Primary key")},
            {QStringLiteral("users_email_key"), QStringLiteral("email"), QStringLiteral("Unique")}};
        for (const QStringList &row : indexes) {
            QList<QStandardItem *> items;
            for (const QString &cell : row) {
                auto *item = new QStandardItem(cell);
                item->setEditable(false);
                items.append(item);
            }
            items.constFirst()->setIcon(toolbarIcon(QStringLiteral("key-round")));
            indexModel_->appendRow(items);
        }

        const QList<QStringList> foreignKeys{
            {QStringLiteral("users_org_id_fkey"), QStringLiteral("organization_id"), QStringLiteral("organizations(id)")}};
        for (const QStringList &row : foreignKeys) {
            QList<QStandardItem *> items;
            for (const QString &cell : row) {
                auto *item = new QStandardItem(cell);
                item->setEditable(false);
                items.append(item);
            }
            items.constFirst()->setIcon(toolbarIcon(QStringLiteral("key-round")));
            foreignKeyModel_->appendRow(items);
        }
    }

}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/mainSplitter"), mainSplitter_->saveState());
    QMainWindow::closeEvent(event);
}

} // namespace vsdb

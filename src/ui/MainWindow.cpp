#include "ui/MainWindow.h"

#include "app/WorkspaceController.h"
#include "core/MockWorkspaceData.h"
#include "ui/CommandRegistry.h"
#include "ui/ConnectionExplorer.h"
#include "ui/InspectorPane.h"
#include "ui/QueryEditorTab.h"
#include "ui/QueryWorkspace.h"
#include "ui/ResultsPane.h"
#include <QAction>
#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace vsdb {

MainWindow::MainWindow(QWidget *parent, bool restoreSettings)
    : QMainWindow(parent), restoreSettings_(restoreSettings), controller_(new WorkspaceController(this)), commands_(new CommandRegistry(this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("VsDB — Database Workbench Preview"));
    resize(1280, 760);
    setMinimumSize(960, 640);

    explorer_ = new ConnectionExplorer(controller_->schemaModel(), this);
    inspector_ = new InspectorPane(controller_->inspectorModel(), this);
    searchBox_ = new QLineEdit(this);
    searchBox_->setObjectName(QStringLiteral("schemaSearch"));
    searchBox_->setAccessibleName(tr("Filter connections and schemas"));
    searchBox_->setPlaceholderText(tr("Search (Ctrl+K)"));
    searchBox_->setClearButtonEnabled(true);
    workspace_ = new QueryWorkspace(controller_->resultModel(), this);
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->setObjectName(QStringLiteral("mainSplitter"));
    mainSplitter_->setChildrenCollapsible(false);
    mainSplitter_->addWidget(explorer_);
    mainSplitter_->addWidget(workspace_);
    mainSplitter_->addWidget(inspector_);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 0);
    mainSplitter_->setSizes({260, 900, 280});
    setCentralWidget(mainSplitter_);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 0);

    createMenus();
    createToolBar();
    connect(searchBox_, &QLineEdit::textChanged, explorer_, &ConnectionExplorer::setFilterText);
    auto *focusSearch = new QAction(this);
    focusSearch->setShortcut(QKeySequence::Find);
    addAction(focusSearch);
    connect(focusSearch, &QAction::triggered, searchBox_, qOverload<>(&QWidget::setFocus));
    statusBar()->setObjectName(QStringLiteral("statusBar"));
    auto *connection = new QLabel(tr("●  Mock connection   PostgreSQL 15   localhost:5432   analytics / public"), this);
    connection->setObjectName(QStringLiteral("connectionStatus"));
    statusState_ = new QLabel(tr("Ready"), this);
    statusState_->setObjectName(QStringLiteral("statusState"));
    statusMetrics_ = new QLabel(tr("UI Preview"), this);
    statusMetrics_->setObjectName(QStringLiteral("statusMetrics"));
    statusBar()->addWidget(connection);
    statusBar()->addWidget(statusState_, 1);
    statusBar()->addPermanentWidget(statusMetrics_);

    connectCommands();
    connect(explorer_, &ConnectionExplorer::objectSelected, controller_, &WorkspaceController::selectObject);
    connect(explorer_, &ConnectionExplorer::tableActivated, this, [this](const QString &table) { workspace_->newQuery(mock::sampleQuery(table)); });
    connect(controller_, &WorkspaceController::selectionChanged, inspector_, &InspectorPane::setSelection);
    connect(controller_, &WorkspaceController::statusChanged, statusState_, &QLabel::setText);
    connect(controller_, &WorkspaceController::executionStarted, this, [this] {
        commands_->runQuery()->setEnabled(false);
        commands_->stopQuery()->setEnabled(true);
        if (workspace_->currentEditor()) workspace_->currentEditor()->resultsPane()->beginExecution();
    });
    connect(controller_, &WorkspaceController::messageAdded, this, [this](const QString &message) {
        if (workspace_->currentEditor()) workspace_->currentEditor()->resultsPane()->appendMessage(message);
    });
    connect(controller_, &WorkspaceController::executionFinished, this, [this](int rows, int elapsed) {
        commands_->runQuery()->setEnabled(true);
        commands_->stopQuery()->setEnabled(false);
        statusMetrics_->setText(tr("Rows: %1   Elapsed: %2 ms   UI Preview").arg(rows).arg(elapsed));
        if (workspace_->currentEditor()) workspace_->currentEditor()->resultsPane()->finishExecution(rows, elapsed);
    });

    controller_->selectObject(QStringLiteral("table.users"), QStringLiteral("users"));
    if (restoreSettings_)
        restoreWindowSettings();
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus()
{
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(commands_->newQuery());
    file->addAction(commands_->closeQuery());
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);
    auto *query = menuBar()->addMenu(tr("&Query"));
    query->addAction(commands_->runQuery());
    query->addAction(commands_->stopQuery());
    query->addAction(commands_->clearEditor());
    auto *view = menuBar()->addMenu(tr("&View"));
    view->addAction(commands_->toggleExplorer());
    view->addAction(commands_->toggleInspector());
    view->addAction(commands_->toggleStatusBar());
    auto *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(commands_->about());
}

void MainWindow::createToolBar()
{
    auto *toolbar = addToolBar(tr("Main toolbar"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *brand = new QLabel(tr("  VsDB  "), toolbar);
    brand->setObjectName(QStringLiteral("brandLabel"));
    toolbar->addWidget(brand);
    toolbar->addSeparator();
    toolbar->addAction(commands_->newQuery());
    toolbar->addAction(commands_->runQuery());
    toolbar->addAction(commands_->stopQuery());
    toolbar->addAction(commands_->refresh());
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    searchBox_->setParent(toolbar);
    searchBox_->setMaximumWidth(300);
    searchBox_->setMinimumWidth(220);
    toolbar->addWidget(searchBox_);
}

void MainWindow::connectCommands()
{
    connect(commands_->newQuery(), &QAction::triggered, workspace_, [this] { workspace_->newQuery(); });
    connect(commands_->closeQuery(), &QAction::triggered, workspace_, &QueryWorkspace::closeCurrentQuery);
    connect(commands_->runQuery(), &QAction::triggered, this, &MainWindow::executeCurrentQuery);
    connect(commands_->stopQuery(), &QAction::triggered, controller_, &WorkspaceController::stopQuery);
    connect(commands_->refresh(), &QAction::triggered, this, [this] { controller_->refreshSchema(); explorer_->expandDefaults(); });
    connect(commands_->clearEditor(), &QAction::triggered, this, [this] { if (workspace_->currentEditor()) workspace_->currentEditor()->clearEditor(); });
    connect(commands_->toggleExplorer(), &QAction::toggled, this, [this](bool visible) { setPaneVisible(explorer_, visible, 260); });
    connect(commands_->toggleInspector(), &QAction::toggled, this, [this](bool visible) { setPaneVisible(inspector_, visible, 280); });
    connect(commands_->toggleStatusBar(), &QAction::toggled, statusBar(), &QWidget::setVisible);
    connect(commands_->about(), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About VsDB"), tr("<b>VsDB</b><br>A lightweight database workbench UI preview.<br><br>This version uses deterministic mock data and does not connect to a database."));
    });
}

void MainWindow::executeCurrentQuery()
{
    if (auto *editor = workspace_->currentEditor()) {
        editor->setModified(false);
        controller_->executeQuery(editor->sql());
    }
}

void MainWindow::restoreWindowSettings()
{
    QSettings settings;
    const auto geometry = settings.value(QStringLiteral("MainWindow/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    mainSplitter_->restoreState(settings.value(QStringLiteral("Workbench/mainSplitterV2")).toByteArray());
    workspace_->restoreCurrentSplitterState(settings.value(QStringLiteral("Workbench/querySplitter")).toByteArray());
    commands_->toggleExplorer()->setChecked(settings.value(QStringLiteral("View/explorerVisible"), true).toBool());
    commands_->toggleInspector()->setChecked(settings.value(QStringLiteral("View/inspectorVisible"), true).toBool());
    commands_->toggleStatusBar()->setChecked(settings.value(QStringLiteral("View/statusBarVisible"), true).toBool());
}

void MainWindow::saveWindowSettings()
{
    if (!restoreSettings_)
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("Workbench/mainSplitterV2"), mainSplitter_->saveState());
    settings.setValue(QStringLiteral("Workbench/querySplitter"), workspace_->currentSplitterState());
    settings.setValue(QStringLiteral("View/explorerVisible"), explorer_->isVisible());
    settings.setValue(QStringLiteral("View/inspectorVisible"), inspector_->isVisible());
    settings.setValue(QStringLiteral("View/statusBarVisible"), statusBar()->isVisible());
}

void MainWindow::setPaneVisible(QWidget *pane, bool visible, int preferredWidth)
{
    pane->setVisible(visible);
    if (!visible)
        return;

    auto sizes = mainSplitter_->sizes();
    const int index = mainSplitter_->indexOf(pane);
    if (index >= 0 && index < sizes.size()) {
        sizes[index] = preferredWidth;
        mainSplitter_->setSizes(sizes);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowSettings();
    QMainWindow::closeEvent(event);
}

} // namespace vsdb

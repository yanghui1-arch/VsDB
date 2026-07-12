#include "ui/CommandRegistry.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>

namespace vsdb {

CommandRegistry::CommandRegistry(QObject *parent) : QObject(parent)
{
    newQuery_ = makeAction(tr("New Query"), QStringLiteral(":/vsdb/icons/add.svg"), QKeySequence::New);
    runQuery_ = makeAction(tr("Run"), QStringLiteral(":/vsdb/icons/run.svg"), QKeySequence(QStringLiteral("Ctrl+Enter")));
    runQuery_->setObjectName(QStringLiteral("runQueryAction"));
    runQuery_->setToolTip(tr("Run the current query (Ctrl+Enter)"));
    stopQuery_ = makeAction(tr("Stop"), QStringLiteral(":/vsdb/icons/stop.svg"));
    stopQuery_->setEnabled(false);
    refresh_ = makeAction(tr("Refresh"), QStringLiteral(":/vsdb/icons/refresh.svg"), QKeySequence::Refresh);
    clearEditor_ = makeAction(tr("Clear Editor"), QStringLiteral(":/vsdb/icons/clear.svg"));
    closeQuery_ = makeAction(tr("Close Query"), {}, QKeySequence::Close);
    toggleExplorer_ = makeAction(tr("Connection Explorer"), {});
    toggleExplorer_->setObjectName(QStringLiteral("toggleExplorerAction"));
    toggleInspector_ = makeAction(tr("Inspector"), {});
    toggleInspector_->setObjectName(QStringLiteral("toggleInspectorAction"));
    toggleStatusBar_ = makeAction(tr("Status Bar"), {});
    toggleStatusBar_->setObjectName(QStringLiteral("toggleStatusBarAction"));
    for (auto *action : {toggleExplorer_, toggleInspector_, toggleStatusBar_}) {
        action->setCheckable(true);
        action->setChecked(true);
    }
    about_ = makeAction(tr("About VsDB"), QStringLiteral(":/vsdb/icons/database.svg"));
}

QAction *CommandRegistry::makeAction(const QString &text, const QString &iconPath, const QKeySequence &shortcut)
{
    auto *action = new QAction(iconPath.isEmpty() ? QIcon() : QIcon(iconPath), text, this);
    action->setShortcut(shortcut);
    action->setStatusTip(text);
    return action;
}

QAction *CommandRegistry::newQuery() const { return newQuery_; }
QAction *CommandRegistry::runQuery() const { return runQuery_; }
QAction *CommandRegistry::stopQuery() const { return stopQuery_; }
QAction *CommandRegistry::refresh() const { return refresh_; }
QAction *CommandRegistry::clearEditor() const { return clearEditor_; }
QAction *CommandRegistry::closeQuery() const { return closeQuery_; }
QAction *CommandRegistry::toggleExplorer() const { return toggleExplorer_; }
QAction *CommandRegistry::toggleInspector() const { return toggleInspector_; }
QAction *CommandRegistry::toggleStatusBar() const { return toggleStatusBar_; }
QAction *CommandRegistry::about() const { return about_; }

} // namespace vsdb

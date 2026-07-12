#include "ui/QueryWorkspace.h"

#include "core/MockWorkspaceData.h"
#include "ui/QueryEditorTab.h"
#include <QMessageBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace vsdb {

QueryWorkspace::QueryWorkspace(ResultTableModel *model, QWidget *parent) : QWidget(parent), model_(model)
{
    setObjectName(QStringLiteral("queryWorkspace"));
    setMinimumWidth(480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("queryTabs"));
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    layout->addWidget(tabs_);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &QueryWorkspace::closeQuery);
    newQuery(mock::sampleQuery());
}

QueryEditorTab *QueryWorkspace::currentEditor() const { return qobject_cast<QueryEditorTab *>(tabs_->currentWidget()); }
int QueryWorkspace::tabCount() const { return tabs_->count(); }

void QueryWorkspace::newQuery(const QString &sql)
{
    const int number = nextQueryNumber_++;
    auto *editor = new QueryEditorTab(model_, sql, tabs_);
    editor->setProperty("queryNumber", number);
    const int index = tabs_->addTab(editor, tr("Query %1").arg(number));
    tabs_->setCurrentIndex(index);
    connect(editor, &QueryEditorTab::modificationChanged, this, [this, editor](bool modified) { updateTabTitle(editor, modified); });
}

void QueryWorkspace::closeCurrentQuery() { closeQuery(tabs_->currentIndex()); }

void QueryWorkspace::closeQuery(int index)
{
    if (index < 0)
        return;
    auto *editor = qobject_cast<QueryEditorTab *>(tabs_->widget(index));
    if (editor && editor->isModified()) {
        const auto answer = QMessageBox::question(this, tr("Discard query changes?"), tr("This preview does not save query files. Discard the changes?"), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Discard)
            return;
    }
    QWidget *widget = tabs_->widget(index);
    tabs_->removeTab(index);
    widget->deleteLater();
    if (tabs_->count() == 0)
        newQuery();
}

void QueryWorkspace::updateTabTitle(QueryEditorTab *editor, bool modified)
{
    const int index = tabs_->indexOf(editor);
    if (index >= 0)
        tabs_->setTabText(index, tr("Query %1%2").arg(editor->property("queryNumber").toInt()).arg(modified ? QStringLiteral(" *") : QString()));
}

QByteArray QueryWorkspace::currentSplitterState() const { return currentEditor() ? currentEditor()->splitterState() : QByteArray(); }
void QueryWorkspace::restoreCurrentSplitterState(const QByteArray &state) { if (currentEditor()) currentEditor()->restoreSplitterState(state); }

} // namespace vsdb

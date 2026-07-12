#include "ui/QueryEditorTab.h"

#include "ui/ResultsPane.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextDocument>
#include <QVBoxLayout>

namespace vsdb {

QueryEditorTab::QueryEditorTab(ResultTableModel *model, const QString &sql, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("queryEditorTab"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    splitter_ = new QSplitter(Qt::Vertical, this);
    splitter_->setObjectName(QStringLiteral("querySplitter"));
    splitter_->setChildrenCollapsible(false);
    editor_ = new QPlainTextEdit(splitter_);
    editor_->setObjectName(QStringLiteral("sqlEditor"));
    editor_->setAccessibleName(tr("SQL editor"));
    editor_->setPlaceholderText(tr("Write a SQL query…"));
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor_->setTabStopDistance(QFontMetricsF(editor_->font()).horizontalAdvance(QLatin1Char(' ')) * 4.0);
    editor_->setPlainText(sql);
    editor_->document()->setModified(false);
    resultsPane_ = new ResultsPane(model, splitter_);
    splitter_->addWidget(editor_);
    splitter_->addWidget(resultsPane_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);
    splitter_->setSizes({430, 330});
    layout->addWidget(splitter_);
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &QueryEditorTab::modificationChanged);
}

QString QueryEditorTab::sql() const { return editor_->toPlainText(); }
void QueryEditorTab::clearEditor() { editor_->clear(); editor_->setFocus(); }
bool QueryEditorTab::isModified() const { return editor_->document()->isModified(); }
void QueryEditorTab::setModified(bool modified) { editor_->document()->setModified(modified); }
ResultsPane *QueryEditorTab::resultsPane() const { return resultsPane_; }
QByteArray QueryEditorTab::splitterState() const { return splitter_->saveState(); }
void QueryEditorTab::restoreSplitterState(const QByteArray &state) { if (!state.isEmpty()) splitter_->restoreState(state); }

} // namespace vsdb

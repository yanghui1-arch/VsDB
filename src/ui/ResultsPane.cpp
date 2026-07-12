#include "ui/ResultsPane.h"

#include "models/ResultTableModel.h"
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

namespace vsdb {

ResultsPane::ResultsPane(ResultTableModel *model, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("resultsPane"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("outputTabs"));
    auto *results = new QWidget(tabs_);
    auto *resultsLayout = new QVBoxLayout(results);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(0);
    tableView_ = new QTableView(results);
    tableView_->setObjectName(QStringLiteral("resultTable"));
    tableView_->setAccessibleName(tr("Query results"));
    tableView_->setModel(model);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setSortingEnabled(false);
    tableView_->verticalHeader()->setVisible(false);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    resultsLayout->addWidget(tableView_, 1);
    summary_ = new QLabel(tr("Run the sample query to load preview data"), results);
    summary_->setObjectName(QStringLiteral("resultSummary"));
    resultsLayout->addWidget(summary_);
    tabs_->addTab(results, tr("Results"));
    messages_ = new QPlainTextEdit(tabs_);
    messages_->setObjectName(QStringLiteral("queryMessages"));
    messages_->setReadOnly(true);
    messages_->setPlaceholderText(tr("Query messages appear here."));
    tabs_->addTab(messages_, tr("Messages"));
    layout->addWidget(tabs_);
}

void ResultsPane::beginExecution()
{
    summary_->setText(tr("Running preview query…"));
    tabs_->setCurrentIndex(1);
}

void ResultsPane::finishExecution(int rows, int elapsedMs)
{
    summary_->setText(tr("%1 rows · %2 ms · bounded preview result").arg(rows).arg(elapsedMs));
    tabs_->setCurrentIndex(rows > 0 ? 0 : 1);
    if (rows > 0)
        tableView_->resizeColumnsToContents();
}

void ResultsPane::appendMessage(const QString &message) { messages_->appendPlainText(message); }
QTableView *ResultsPane::tableView() const { return tableView_; }

} // namespace vsdb

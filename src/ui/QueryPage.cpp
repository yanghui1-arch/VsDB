#include "ui/QueryPage.h"

#include "SqlEditor.h"
#include "UiComponents.h"
#include "WorkbenchModels.h"
#include "ui/IconProvider.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTabBar>
#include <QTableView>
#include <QTextCursor>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStringConverter>

#include <utility>

namespace vsdb {

namespace {

QToolButton *plainButton(QWidget *parent, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("plainButton"));
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(false);
    return button;
}

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

QString csvCell(const QVariant &value)
{
    if (value.isNull())
        return {};
    QString text = value.toString();
    if (text.contains(QLatin1Char('"')))
        text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"'))
        || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')))
        text = QLatin1Char('"') + text + QLatin1Char('"');
    return text;
}

} // namespace

QueryPage::QueryPage(const QString &sql, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("queryPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    splitter_ = new QSplitter(Qt::Vertical, this);
    editor_ = new SqlEditor(splitter_);
    editor_->setPlainText(sql);
    splitter_->addWidget(editor_);

    auto *results = new QWidget(splitter_);
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
    auto *gridButton = plainButton(tools, QString{}, QStringLiteral("表格视图"));
    gridButton->setIcon(databaseIcon(QStringLiteral("layout-grid")));
    toolLayout->addWidget(gridButton);
    auto *exportButton = new QPushButton(databaseIcon(QStringLiteral("upload")),
                                         QStringLiteral("Export"), tools);
    exportButton->setToolTip(QStringLiteral("导出当前结果为 CSV"));
    toolLayout->addSpacing(6);
    toolLayout->addWidget(exportButton);
    auto *copyButton = new QPushButton(databaseIcon(QStringLiteral("copy")),
                                       QStringLiteral("Copy"), tools);
    toolLayout->addWidget(copyButton);
    toolLayout->addSpacing(6);
    commitButton_ = new QPushButton(databaseIcon(QStringLiteral("save")),
                                    QStringLiteral("Commit"), tools);
    commitButton_->setToolTip(QStringLiteral("在一个事务中提交所有单元格修改"));
    commitButton_->setEnabled(false);
    toolLayout->addWidget(commitButton_);
    rollbackButton_ = new QPushButton(databaseIcon(QStringLiteral("undo-2")),
                                      QStringLiteral("Rollback"), tools);
    rollbackButton_->setToolTip(QStringLiteral("放弃所有待提交修改"));
    rollbackButton_->setEnabled(false);
    toolLayout->addWidget(rollbackButton_);
    toolLayout->addStretch();
    rowLimit_ = new ModernComboBox(tools);
    rowLimit_->setObjectName(QStringLiteral("rowLimitCombo"));
    rowLimit_->addItem(QStringLiteral("50 rows"), 50);
    rowLimit_->addItem(QStringLiteral("100 rows"), 100);
    rowLimit_->addItem(QStringLiteral("500 rows"), 500);
    rowLimit_->setCurrentIndex(1);
    rowLimit_->setFixedWidth(112);
    toolLayout->addWidget(rowLimit_);
    resultsLayout->addWidget(tools);

    model_ = new ResultTableModel(this);
    table_ = new QTableView(results);
    table_->setHorizontalHeader(new TwoLineHeaderView(Qt::Horizontal, table_));
    table_->setVerticalHeader(new ResultRowHeaderView(table_));
    table_->setModel(model_);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked);
    table_->setSortingEnabled(false);
    table_->verticalHeader()->setDefaultSectionSize(37);
    table_->verticalHeader()->setMinimumWidth(38);
    table_->horizontalHeader()->setFixedHeight(49);
    table_->horizontalHeader()->setStretchLastSection(true);
    resultsLayout->addWidget(table_, 1);

    messages_ = new QPlainTextEdit(results);
    messages_->setReadOnly(true);
    messages_->setVisible(false);
    resultsLayout->addWidget(messages_, 1);

    summary_ = new QLabel(QStringLiteral("尚未执行查询"), results);
    summary_->setObjectName(QStringLiteral("muted"));
    summary_->setContentsMargins(4, 5, 0, 0);
    resultsLayout->addWidget(summary_);

    splitter_->addWidget(results);
    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(0, 2);
    splitter_->setStretchFactor(1, 3);
    splitter_->setSizes({320, 450});
    root->addWidget(splitter_);

    connect(table_->verticalHeader(), &QHeaderView::sectionClicked, this, [this](int row) {
        table_->setCurrentIndex(model_->index(row, 0));
        const QItemSelection entireRow(model_->index(row, 0),
                                       model_->index(row, model_->columnCount() - 1));
        table_->selectionModel()->select(entireRow, QItemSelectionModel::ClearAndSelect);
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
            table_->verticalHeader()->viewport(), qOverload<>(&QWidget::update));
    connect(model_, &QAbstractItemModel::dataChanged, this, [this] { updateTransactionState(); });
    connect(commitButton_, &QPushButton::clicked, this, &QueryPage::commitRequested);
    connect(rollbackButton_, &QPushButton::clicked, this, [this] {
        const int changes = model_->pendingChangeCount();
        model_->rollbackPendingChanges();
        setStatus(QStringLiteral("已放弃 %1 处待提交修改").arg(changes));
        updateTransactionState();
    });
    connect(rowLimit_, &QComboBox::currentIndexChanged, this, [this] {
        setStatus(QStringLiteral("行数上限已设为 %1，将在下次查询时生效").arg(rowLimit()));
    });
    connect(copyButton, &QPushButton::clicked, this, [this] {
        const QModelIndexList selected = table_->selectionModel()->selectedIndexes();
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
    connect(exportButton, &QPushButton::clicked, this, &QueryPage::exportCsv);
    connect(resultTabs, &QTabBar::currentChanged, this, [this](int index) {
        table_->setVisible(index == 0);
        messages_->setVisible(index == 1);
    });
}

SqlEditor *QueryPage::editor() const
{
    return editor_;
}

ResultTableModel *QueryPage::resultModel() const
{
    return model_;
}

QString QueryPage::selectedSql() const
{
    const QTextCursor cursor = editor_->textCursor();
    return cursor.hasSelection() ? cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'))
                                 : editor_->toPlainText();
}

int QueryPage::rowLimit() const
{
    return rowLimit_->currentData().toInt();
}

void QueryPage::beginExecution()
{
    elapsed_.start();
    setStatus(QStringLiteral("正在执行查询…"));
}

qint64 QueryPage::elapsedMilliseconds() const
{
    return elapsed_.isValid() ? elapsed_.elapsed() : 0;
}

void QueryPage::setQueryResult(QueryResult result, bool editable)
{
    const int rowCount = result.rows.size();
    const qlonglong affectedRows = result.affectedRows;
    const bool select = result.select;
    const bool truncated = result.truncated;
    model_->setResult(std::move(result), editable);
    table_->resizeColumnsToContents();
    for (int column = 0; column < model_->columnCount(); ++column)
        table_->setColumnWidth(column, qBound(72, table_->columnWidth(column), 240));

    QString message;
    if (select) {
        message = QStringLiteral("查询完成 · %1 行 · %2 毫秒").arg(rowCount).arg(elapsedMilliseconds());
        if (truncated)
            message += QStringLiteral(" · 已达到显示上限");
    } else {
        message = QStringLiteral("命令执行成功 · 影响 %1 行 · %2 毫秒")
                      .arg(affectedRows).arg(elapsedMilliseconds());
    }
    messages_->setPlainText(message);
    setStatus(message);
    updateTransactionState();
}

void QueryPage::setStatus(const QString &message)
{
    summary_->setText(message);
    messages_->setPlainText(message);
}

void QueryPage::setTableContext(const QString &schema, const QString &table,
                                const QStringList &primaryKeys)
{
    tableSchema_ = schema;
    tableName_ = table;
    primaryKeys_ = primaryKeys;
}

void QueryPage::clearTableContext()
{
    tableSchema_.clear();
    tableName_.clear();
    primaryKeys_.clear();
}

QString QueryPage::tableSchema() const
{
    return tableSchema_;
}

QString QueryPage::tableName() const
{
    return tableName_;
}

QStringList QueryPage::primaryKeys() const
{
    return primaryKeys_;
}

void QueryPage::updateTransactionState()
{
    const int changes = model_->pendingChangeCount();
    commitButton_->setEnabled(changes > 0);
    rollbackButton_->setEnabled(changes > 0);
    commitButton_->setText(changes > 0
        ? QStringLiteral("Commit (%1)").arg(changes) : QStringLiteral("Commit"));
}

void QueryPage::exportCsv()
{
    if (model_->rowCount() == 0 || model_->columnCount() == 0) {
        setStatus(QStringLiteral("当前没有可导出的结果"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出查询结果"),
                                                      QStringLiteral("result.csv"),
                                                      QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("无法写入文件：%1").arg(file.errorString()));
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (int column = 0; column < model_->columnCount(); ++column) {
        if (column)
            stream << QLatin1Char(',');
        stream << csvCell(model_->columns().at(column).name);
    }
    stream << QLatin1Char('\n');
    for (int row = 0; row < model_->rowCount(); ++row) {
        for (int column = 0; column < model_->columnCount(); ++column) {
            if (column)
                stream << QLatin1Char(',');
            stream << csvCell(model_->index(row, column).data(Qt::EditRole));
        }
        stream << QLatin1Char('\n');
    }
    if (!file.commit()) {
        setStatus(QStringLiteral("导出失败：%1").arg(file.errorString()));
        return;
    }
    setStatus(QStringLiteral("已导出 %1 行到 %2").arg(model_->rowCount()).arg(path));
}

} // namespace vsdb

#pragma once

#include "database/DatabaseTypes.h"

#include <QElapsedTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTableView;

namespace vsdb {

class ResultTableModel;
class SqlEditor;

class QueryPage final : public QWidget
{
    Q_OBJECT

public:
    explicit QueryPage(const QString &sql, QWidget *parent = nullptr);

    SqlEditor *editor() const;
    ResultTableModel *resultModel() const;
    QString selectedSql() const;
    int rowLimit() const;

    void beginExecution();
    qint64 elapsedMilliseconds() const;
    bool isExecuting() const;
    void beginResult(QVector<QueryColumn> columns, bool select, bool editable);
    void appendResultRows(QVector<QVariantList> rows, qint64 loadedBytes);
    void finishExecution(qlonglong affectedRows, bool truncated,
                         bool memoryLimited, bool cancelled,
                         qint64 loadedBytes, const QString &error = {});
    void setQueryResult(QueryResult result, bool editable);
    void setStatus(const QString &message);
    void setResultNotice(const QString &notice);

    void setTableContext(const QString &schema, const QString &table,
                         const QStringList &primaryKeys);
    void clearTableContext();
    QString tableSchema() const;
    QString tableName() const;
    QStringList primaryKeys() const;

signals:
    void commitRequested();

private:
    void configureResultColumns();
    void updateTransactionState();
    void exportCsv();

    SqlEditor *editor_ = nullptr;
    ResultTableModel *model_ = nullptr;
    QTableView *table_ = nullptr;
    QSplitter *splitter_ = nullptr;
    QLabel *summary_ = nullptr;
    QPlainTextEdit *messages_ = nullptr;
    QComboBox *rowLimit_ = nullptr;
    QPushButton *commitButton_ = nullptr;
    QPushButton *rollbackButton_ = nullptr;
    QElapsedTimer elapsed_;
    bool executing_ = false;
    bool selectResult_ = false;
    QString resultNotice_;
    QString tableSchema_;
    QString tableName_;
    QStringList primaryKeys_;
};

} // namespace vsdb

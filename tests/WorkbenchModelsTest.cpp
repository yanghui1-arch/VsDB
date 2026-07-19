#include "WorkbenchModels.h"

#include <QApplication>
#include <iostream>

namespace {

int fail(int code, const char *message)
{
    std::cerr << message << '\n';
    return code;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    vsdb::ResultTableModel model;
    if (model.rowCount() != 0 || model.columnCount() != 0)
        return fail(1, "a new result model must be empty");

    vsdb::QueryResult result;
    result.select = true;
    result.columns = {{QStringLiteral("id"), QStringLiteral("bigint")},
                      {QStringLiteral("email"), QStringLiteral("text")},
                      {QStringLiteral("active"), QStringLiteral("bool")}};
    result.rows = {{QVariant::fromValue<qlonglong>(1), QStringLiteral("alice@example.com"), true},
                   {QVariant::fromValue<qlonglong>(2), QVariant(QMetaType(QMetaType::QString)), false}};
    model.setResult(result, true);
    if (model.rowCount() != 2 || model.columnCount() != 3)
        return fail(2, "result shape was not loaded");
    if (model.headerData(0, Qt::Horizontal).toString() != QStringLiteral("id\nbigint"))
        return fail(3, "column metadata was not exposed");
    if (model.index(1, 1).data().toString() != QStringLiteral("NULL"))
        return fail(4, "null values were not rendered explicitly");

    const QModelIndex email = model.index(0, 1);
    if (!(model.flags(email) & Qt::ItemIsEditable))
        return fail(5, "editable table results should allow edits");
    if (!model.setData(email, QStringLiteral("updated@example.com"))
        || model.pendingChangeCount() != 1
        || email.data().toString() != QStringLiteral("updated@example.com"))
        return fail(6, "staging an edit failed");
    const QVector<vsdb::CellChange> changes = model.pendingChanges();
    if (changes.size() != 1 || changes.constFirst().originalValue.toString()
            != QStringLiteral("alice@example.com"))
        return fail(7, "pending edit did not retain the original value");

    model.rollbackPendingChanges();
    if (model.pendingChangeCount() != 0
        || email.data().toString() != QStringLiteral("alice@example.com"))
        return fail(8, "rolling back an edit failed");
    if (!model.setData(email, QStringLiteral("committed@example.com")))
        return fail(9, "staging a commit failed");
    model.commitPendingChanges();
    if (model.pendingChangeCount() != 0
        || email.data().toString() != QStringLiteral("committed@example.com"))
        return fail(10, "committing an edit failed");
    if (model.setData(model.index(0, 0), QStringLiteral("not-a-number")))
        return fail(11, "invalid numeric edits should be rejected");

    model.setResult(result, false);
    if (model.flags(model.index(0, 0)) & Qt::ItemIsEditable)
        return fail(12, "arbitrary query results must be read-only");

    model.beginResult(result.columns, true, false);
    if (model.rowCount() != 0 || model.columnCount() != 3)
        return fail(13, "beginning a streamed result must expose columns and clear rows");
    model.appendRows({result.rows.constFirst()});
    model.appendRows({result.rows.constLast()});
    if (model.rowCount() != 2 || model.index(1, 0).data(Qt::EditRole).toLongLong() != 2)
        return fail(14, "streamed result batches were not appended in order");

    const QString largeText(4096, QLatin1Char('x'));
    vsdb::QueryResult largeResult;
    largeResult.select = true;
    largeResult.columns = {{QStringLiteral("payload"), QStringLiteral("text")}};
    largeResult.rows = {{largeText}};
    model.setResult(largeResult, false);
    const QModelIndex payload = model.index(0, 0);
    if (payload.data(Qt::DisplayRole).toString().size() >= largeText.size()
        || payload.data(Qt::EditRole).toString() != largeText)
        return fail(15, "large values must be previewed without discarding their full value");
    return 0;
}

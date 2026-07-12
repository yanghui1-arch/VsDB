#include "core/MockWorkspaceData.h"
#include "models/ResultTableModel.h"
#include "models/SchemaTreeModel.h"
#include <QAbstractItemModelTester>
#include <QSortFilterProxyModel>
#include <QtTest>

using namespace vsdb;

class ModelTests final : public QObject {
    Q_OBJECT
private slots:
    void schemaTreeContracts()
    {
        SchemaTreeModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 1);
        const QModelIndex connection = model.index(0, 0);
        QCOMPARE(connection.data().toString(), QStringLiteral("PostgreSQL 15 · localhost:5432"));
        QVERIFY(!model.parent(connection).isValid());
        QVERIFY(model.rowCount(connection) > 0);
        QVERIFY(!(model.flags(connection) & Qt::ItemIsEditable));
    }

    void recursiveFiltering()
    {
        SchemaTreeModel model;
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setRecursiveFilteringEnabled(true);
        proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);
        proxy.setFilterFixedString(QStringLiteral("analytics_summary"));
        QVERIFY(proxy.rowCount() > 0);
    }

    void resultDataAndRoles()
    {
        ResultTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 0);
        model.setResult(mock::queryResult());
        QCOMPARE(model.rowCount(), 7);
        QCOMPARE(model.columnCount(), 6);
        QCOMPARE(model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("email"));
        QCOMPARE(model.index(0, 1).data().toString(), QStringLiteral("maya@example.com"));
        QVERIFY(model.index(0, 0).data(Qt::TextAlignmentRole).isValid());
        QCOMPARE(model.index(5, 3).data().toString(), QStringLiteral("NULL"));
        model.clear();
        QCOMPARE(model.rowCount(), 0);
    }
};

QTEST_MAIN(ModelTests)
#include "ModelTests.moc"

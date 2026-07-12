#include "ui/MainWindow.h"
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QtTest>

using namespace vsdb;

class MainWindowTests final : public QObject {
    Q_OBJECT
private slots:
    void shellAndExecution()
    {
        MainWindow window(nullptr, false);
        window.show();
        QTest::qWait(20);
        QVERIFY(window.isVisible());
        QVERIFY(window.windowTitle().contains(QStringLiteral("VsDB")));
        QVERIFY(!window.windowTitle().contains(QStringLiteral("DataPilot")));
        QVERIFY(window.findChild<QTreeView *>(QStringLiteral("schemaTree")));
        QVERIFY(window.findChild<QTableView *>(QStringLiteral("resultTable")));
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("queryTabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        auto *run = window.findChild<QAction *>(QStringLiteral("runQueryAction"));
        QVERIFY(run);
        auto *inspector = window.findChild<QWidget *>(QStringLiteral("inspectorPane"));
        QVERIFY(inspector);
        QTRY_VERIFY_WITH_TIMEOUT(inspector->width() > 0, 1000);
        auto *toggleInspector = window.findChild<QAction *>(QStringLiteral("toggleInspectorAction"));
        QVERIFY(toggleInspector);
        toggleInspector->setChecked(false);
        QTRY_VERIFY_WITH_TIMEOUT(!inspector->isVisible(), 1000);
        toggleInspector->setChecked(true);
        QTRY_VERIFY_WITH_TIMEOUT(inspector->isVisible() && inspector->width() > 0, 1000);
        run->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QTableView *>(QStringLiteral("resultTable"))->model()->rowCount(), 7, 1000);
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("statusMetrics"))->text().contains(QStringLiteral("Rows: 7")));
    }

    void searchFiltersExplorer()
    {
        MainWindow window(nullptr, false);
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("schemaSearch"));
        auto *tree = window.findChild<QTreeView *>(QStringLiteral("schemaTree"));
        QVERIFY(search);
        QVERIFY(tree);
        search->setText(QStringLiteral("daily_revenue"));
        QVERIFY(tree->model()->rowCount() > 0);
    }
};

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"

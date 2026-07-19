#include "WorkbenchModels.h"

#include <QApplication>
#include <iostream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    vsdb::ResultTableModel model;
    if (model.rowCount() != 100 || model.columnCount() != 6) {
        std::cerr << "unexpected result model shape\n";
        return 1;
    }
    if (model.index(0, 1).data().toString() != QStringLiteral("alice@example.com")) {
        std::cerr << "unexpected deterministic cell value\n";
        return 2;
    }
    model.setVisibleRows(100000);
    if (model.rowCount() != 1000) {
        std::cerr << "upper row bound failed\n";
        return 3;
    }
    model.setVisibleRows(0);
    if (model.rowCount() != 1) {
        std::cerr << "lower row bound failed\n";
        return 4;
    }
    const QModelIndex email = model.index(0, 1);
    if (!(model.flags(email) & Qt::ItemIsEditable)) {
        std::cerr << "result cells should be editable\n";
        return 5;
    }
    if (!model.setData(email, QStringLiteral("updated@example.com"))
        || model.pendingChangeCount() != 1
        || email.data().toString() != QStringLiteral("updated@example.com")) {
        std::cerr << "staging an edit failed\n";
        return 6;
    }
    model.rollbackPendingChanges();
    if (model.pendingChangeCount() != 0
        || email.data().toString() != QStringLiteral("alice@example.com")) {
        std::cerr << "rolling back an edit failed\n";
        return 7;
    }
    if (!model.setData(email, QStringLiteral("committed@example.com"))) {
        std::cerr << "staging a commit failed\n";
        return 8;
    }
    model.commitPendingChanges();
    if (model.pendingChangeCount() != 0
        || email.data().toString() != QStringLiteral("committed@example.com")) {
        std::cerr << "committing an edit failed\n";
        return 9;
    }
    if (model.setData(model.index(0, 0), QStringLiteral("not-a-number"))) {
        std::cerr << "invalid numeric edits should be rejected\n";
        return 10;
    }
    return 0;
}

#pragma once

#include <QWidget>

class QSortFilterProxyModel;
class QTreeView;

namespace vsdb {
class SchemaTreeModel;

class ConnectionExplorer final : public QWidget {
    Q_OBJECT
public:
    explicit ConnectionExplorer(SchemaTreeModel *model, QWidget *parent = nullptr);
    void setFilterText(const QString &text);
    QTreeView *treeView() const;
    void expandDefaults();

signals:
    void objectSelected(const QString &objectId, const QString &displayName);
    void tableActivated(const QString &tableName);

private:
    QTreeView *treeView_;
    QSortFilterProxyModel *proxyModel_;
};

} // namespace vsdb

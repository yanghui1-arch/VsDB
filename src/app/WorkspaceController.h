#pragma once

#include <QObject>

namespace vsdb {
class InspectorModel;
class ResultTableModel;
class SchemaTreeModel;

class WorkspaceController final : public QObject {
    Q_OBJECT
public:
    explicit WorkspaceController(QObject *parent = nullptr);
    SchemaTreeModel *schemaModel() const;
    ResultTableModel *resultModel() const;
    InspectorModel *inspectorModel() const;

public slots:
    void selectObject(const QString &objectId, const QString &displayName);
    void executeQuery(const QString &sql);
    void stopQuery();
    void refreshSchema();

signals:
    void selectionChanged(const QString &name, const QString &subtitle, bool inspectable);
    void executionStarted();
    void executionFinished(int rowCount, int elapsedMs);
    void messageAdded(const QString &message);
    void statusChanged(const QString &status);

private:
    SchemaTreeModel *schemaModel_;
    ResultTableModel *resultModel_;
    InspectorModel *inspectorModel_;
    bool running_ = false;
    quint64 executionToken_ = 0;
};

} // namespace vsdb

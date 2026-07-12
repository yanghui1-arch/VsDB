#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <memory>
#include <vector>

namespace vsdb {

enum class SchemaNodeKind { Connection, Database, Folder, Schema, Table, View, Function };

struct SchemaNode {
    QString id;
    QString name;
    SchemaNodeKind kind = SchemaNodeKind::Folder;
    SchemaNode *parent = nullptr;
    std::vector<std::unique_ptr<SchemaNode>> children;
};

struct ResultColumn {
    QString name;
    QString type;
};

struct QueryResult {
    QList<ResultColumn> columns;
    QList<QList<QVariant>> rows;
    int elapsedMs = 0;
};

struct InspectorProperty {
    QString name;
    QString value;
};

} // namespace vsdb

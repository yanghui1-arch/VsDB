#pragma once

#include "core/WorkspaceData.h"

namespace vsdb::mock {

std::unique_ptr<SchemaNode> schemaTree();
QueryResult queryResult();
QList<InspectorProperty> inspectorProperties(const QString &objectId);
QString sampleQuery(const QString &tableName = QStringLiteral("users"));

} // namespace vsdb::mock

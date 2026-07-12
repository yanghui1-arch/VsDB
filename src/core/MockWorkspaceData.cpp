#include "core/MockWorkspaceData.h"

#include <utility>

namespace vsdb::mock {
namespace {

std::unique_ptr<SchemaNode> node(QString id, QString name, SchemaNodeKind kind, SchemaNode *parent = nullptr)
{
    auto value = std::make_unique<SchemaNode>();
    value->id = std::move(id);
    value->name = std::move(name);
    value->kind = kind;
    value->parent = parent;
    return value;
}

SchemaNode *append(SchemaNode &parent, std::unique_ptr<SchemaNode> child)
{
    child->parent = &parent;
    auto *result = child.get();
    parent.children.push_back(std::move(child));
    return result;
}

} // namespace

std::unique_ptr<SchemaNode> schemaTree()
{
    auto root = node(QStringLiteral("root"), {}, SchemaNodeKind::Folder);
    auto *connection = append(*root, node(QStringLiteral("connection.local"), QStringLiteral("PostgreSQL 15 · localhost:5432"), SchemaNodeKind::Connection));
    auto *databases = append(*connection, node(QStringLiteral("databases"), QStringLiteral("Databases"), SchemaNodeKind::Folder));
    append(*databases, node(QStringLiteral("database.postgres"), QStringLiteral("postgres"), SchemaNodeKind::Database));
    auto *analytics = append(*databases, node(QStringLiteral("database.analytics"), QStringLiteral("analytics"), SchemaNodeKind::Database));
    auto *schemas = append(*analytics, node(QStringLiteral("schemas"), QStringLiteral("Schemas"), SchemaNodeKind::Folder));
    auto *publicSchema = append(*schemas, node(QStringLiteral("schema.public"), QStringLiteral("public"), SchemaNodeKind::Schema));
    auto *tables = append(*publicSchema, node(QStringLiteral("tables"), QStringLiteral("Tables  5"), SchemaNodeKind::Folder));
    append(*tables, node(QStringLiteral("table.users"), QStringLiteral("users"), SchemaNodeKind::Table));
    append(*tables, node(QStringLiteral("table.orders"), QStringLiteral("orders"), SchemaNodeKind::Table));
    append(*tables, node(QStringLiteral("table.products"), QStringLiteral("products"), SchemaNodeKind::Table));
    append(*tables, node(QStringLiteral("table.events"), QStringLiteral("events"), SchemaNodeKind::Table));
    append(*tables, node(QStringLiteral("table.analytics_summary"), QStringLiteral("analytics_summary"), SchemaNodeKind::Table));
    auto *views = append(*publicSchema, node(QStringLiteral("views"), QStringLiteral("Views  2"), SchemaNodeKind::Folder));
    append(*views, node(QStringLiteral("view.active_users"), QStringLiteral("active_users"), SchemaNodeKind::View));
    append(*views, node(QStringLiteral("view.daily_revenue"), QStringLiteral("daily_revenue"), SchemaNodeKind::View));
    append(*publicSchema, node(QStringLiteral("indexes"), QStringLiteral("Indexes"), SchemaNodeKind::Folder));
    append(*publicSchema, node(QStringLiteral("functions"), QStringLiteral("Functions"), SchemaNodeKind::Folder));
    append(*databases, node(QStringLiteral("database.template1"), QStringLiteral("template1"), SchemaNodeKind::Database));
    auto *cache = append(*connection, node(QStringLiteral("cache"), QStringLiteral("Cache  45"), SchemaNodeKind::Folder));
    append(*cache, node(QStringLiteral("cache.session12ab"), QStringLiteral("session:12ab"), SchemaNodeKind::Table));
    append(*cache, node(QStringLiteral("cache.user1001"), QStringLiteral("user:1001"), SchemaNodeKind::Table));
    return root;
}

QueryResult queryResult()
{
    QueryResult result;
    result.columns = {{QStringLiteral("id"), QStringLiteral("bigint")},
                      {QStringLiteral("email"), QStringLiteral("text")},
                      {QStringLiteral("created_at"), QStringLiteral("timestamptz")},
                      {QStringLiteral("order_id"), QStringLiteral("bigint")},
                      {QStringLiteral("total"), QStringLiteral("numeric")},
                      {QStringLiteral("status"), QStringLiteral("text")}};
    result.rows = {
        {1007, QStringLiteral("maya@example.com"), QStringLiteral("2024-05-24 09:42:18"), 8201, 149.90, QStringLiteral("completed")},
        {1006, QStringLiteral("liam@example.com"), QStringLiteral("2024-05-23 16:08:51"), 8198, 79.50, QStringLiteral("shipped")},
        {1005, QStringLiteral("sofia@example.com"), QStringLiteral("2024-05-22 13:17:04"), 8194, 249.00, QStringLiteral("completed")},
        {1004, QStringLiteral("noah@example.com"), QStringLiteral("2024-05-21 08:31:26"), 8189, 32.75, QStringLiteral("cancelled")},
        {1003, QStringLiteral("emma@example.com"), QStringLiteral("2024-05-20 18:55:42"), 8182, 95.20, QStringLiteral("shipped")},
        {1002, QStringLiteral("oliver@example.com"), QStringLiteral("2024-05-19 11:12:03"), {}, {}, QStringLiteral("pending")},
        {1001, QStringLiteral("ava@example.com"), QStringLiteral("2024-05-18 14:39:10"), 8173, 420.00, QStringLiteral("completed")}};
    result.elapsedMs = 48;
    return result;
}

QList<InspectorProperty> inspectorProperties(const QString &objectId)
{
    const QString object = objectId.section('.', -1);
    if (!objectId.startsWith(QStringLiteral("table.")) && !objectId.startsWith(QStringLiteral("view.")))
        return {};
    return {{QStringLiteral("Name"), object},
            {QStringLiteral("Type"), objectId.startsWith(QStringLiteral("view.")) ? QStringLiteral("View") : QStringLiteral("Table")},
            {QStringLiteral("Schema"), QStringLiteral("public")},
            {QStringLiteral("Row estimate"), QStringLiteral("~10,248")},
            {QStringLiteral("Size"), QStringLiteral("2.1 MB")},
            {QStringLiteral("Primary key"), QStringLiteral("id")},
            {QStringLiteral("Description"), QStringLiteral("Preview metadata for %1").arg(object)}};
}

QString sampleQuery(const QString &tableName)
{
    if (tableName == QStringLiteral("users")) {
        return QStringLiteral("SELECT\n  u.id,\n  u.email,\n  u.created_at,\n  o.id AS order_id,\n  o.total,\n  o.status\nFROM public.users u\nLEFT JOIN public.orders o ON o.user_id = u.id\nWHERE u.created_at >= '2024-01-01'\nORDER BY u.created_at DESC\nLIMIT 100;");
    }
    return QStringLiteral("SELECT *\nFROM public.%1\nLIMIT 100;").arg(tableName);
}

} // namespace vsdb::mock

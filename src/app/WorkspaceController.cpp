#include "app/WorkspaceController.h"

#include "core/MockWorkspaceData.h"
#include "models/InspectorModel.h"
#include "models/ResultTableModel.h"
#include "models/SchemaTreeModel.h"
#include <QTimer>

namespace vsdb {

WorkspaceController::WorkspaceController(QObject *parent)
    : QObject(parent), schemaModel_(new SchemaTreeModel(this)), resultModel_(new ResultTableModel(this)), inspectorModel_(new InspectorModel(this))
{
    selectObject(QStringLiteral("table.users"), QStringLiteral("users"));
}

SchemaTreeModel *WorkspaceController::schemaModel() const { return schemaModel_; }
ResultTableModel *WorkspaceController::resultModel() const { return resultModel_; }
InspectorModel *WorkspaceController::inspectorModel() const { return inspectorModel_; }

void WorkspaceController::selectObject(const QString &objectId, const QString &displayName)
{
    const auto properties = mock::inspectorProperties(objectId);
    inspectorModel_->setProperties(properties);
    const bool inspectable = !properties.isEmpty();
    emit selectionChanged(displayName, inspectable ? QStringLiteral("public.%1").arg(displayName) : tr("Select a table or view"), inspectable);
}

void WorkspaceController::executeQuery(const QString &sql)
{
    if (running_ || sql.trimmed().isEmpty())
        return;
    running_ = true;
    const quint64 token = ++executionToken_;
    resultModel_->clear();
    emit executionStarted();
    emit messageAdded(tr("Running query in UI preview mode…"));
    emit statusChanged(tr("Running preview query"));
    QTimer::singleShot(180, this, [this, token] {
        if (!running_ || token != executionToken_)
            return;
        const auto result = mock::queryResult();
        const int rows = result.rows.size();
        const int elapsed = result.elapsedMs;
        resultModel_->setResult(result);
        running_ = false;
        emit messageAdded(tr("Preview query completed: %1 rows in %2 ms.").arg(rows).arg(elapsed));
        emit executionFinished(rows, elapsed);
        emit statusChanged(tr("Query completed"));
    });
}

void WorkspaceController::stopQuery()
{
    if (!running_)
        return;
    running_ = false;
    ++executionToken_;
    emit messageAdded(tr("Preview query stopped."));
    emit executionFinished(0, 0);
    emit statusChanged(tr("Query stopped"));
}

void WorkspaceController::refreshSchema()
{
    schemaModel_->reload();
    emit statusChanged(tr("Preview schema refreshed"));
}

} // namespace vsdb

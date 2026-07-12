#include "app/Application.h"

#include <QFile>
#include <QLoggingCategory>

namespace vsdb {

Application::Application(int &argc, char **argv) : QApplication(argc, argv)
{
    setOrganizationName(QStringLiteral("VsDB"));
    setOrganizationDomain(QStringLiteral("vsdb.dev"));
    setApplicationName(QStringLiteral("VsDB"));
    setApplicationDisplayName(QStringLiteral("VsDB"));
    setApplicationVersion(QStringLiteral(VSDB_VERSION));
    applyStyleSheet();
}

void Application::applyStyleSheet()
{
    QFile file(QStringLiteral(":/vsdb/styles/vsdb.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Unable to load the VsDB stylesheet; using the platform style.");
        return;
    }
    setStyleSheet(QString::fromUtf8(file.readAll()));
}

} // namespace vsdb

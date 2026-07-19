#include "MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QCoreApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QPixmap>
#include <QPainter>
#include <QEventLoop>
#include <QProxyStyle>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>

namespace {
class StableFusionStyle final : public QProxyStyle
{
public:
    StableFusionStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == QStyle::SH_Widget_Animate || hint == QStyle::SH_ScrollBar_Transient ||
            hint == QStyle::SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VsDB"));
    QApplication::setOrganizationName(QStringLiteral("VsDB"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(new StableFusionStyle);

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(10);
    app.setFont(font);

    QFile theme(QCoreApplication::applicationDirPath() + QStringLiteral("/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));

    QString screenshotPath = qEnvironmentVariable("VSDB_SCREENSHOT");
    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotArgument = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotPath.isEmpty() && screenshotArgument >= 0 && screenshotArgument + 1 < arguments.size())
        screenshotPath = arguments.at(screenshotArgument + 1);

    vsdb::MainWindow window;
    if (!screenshotPath.isEmpty()) {
        const QString screenshotHost = qEnvironmentVariable("VSDB_SCREENSHOT_PG_HOST");
        if (!screenshotHost.isEmpty()) {
            vsdb::PostgresConnectionConfig config;
            config.host = screenshotHost;
            config.port = qEnvironmentVariableIntValue("VSDB_SCREENSHOT_PG_PORT");
            if (config.port <= 0)
                config.port = 5432;
            config.user = qEnvironmentVariable("VSDB_SCREENSHOT_PG_USER", QStringLiteral("postgres"));
            config.password = qEnvironmentVariable("VSDB_SCREENSHOT_PG_PASSWORD");
            config.database = qEnvironmentVariable("VSDB_SCREENSHOT_PG_DATABASE", QStringLiteral("postgres"));
            config.sslMode = qEnvironmentVariable("VSDB_SCREENSHOT_PG_SSLMODE", QStringLiteral("prefer"));
            QString error;
            if (!window.connectToPostgres(config, &error))
                return 3;
            const QString relation = qEnvironmentVariable("VSDB_SCREENSHOT_PG_RELATION");
            if (!relation.isEmpty()) {
                const QString schema = qEnvironmentVariable("VSDB_SCREENSHOT_PG_SCHEMA", QStringLiteral("public"));
                if (!window.openRelationPreview(schema, relation, &error))
                    return 4;
            }
        }
        window.resize(1560, 920);
        window.show();
        QEventLoop paintLoop;
        QTimer::singleShot(400, &paintLoop, &QEventLoop::quit);
        paintLoop.exec();
        const bool openCombo = qEnvironmentVariableIsSet("VSDB_OPEN_COMBO");
        if (openCombo) {
            if (auto *combo = window.findChild<QComboBox *>(QStringLiteral("rowLimitCombo")))
                combo->showPopup();
            QEventLoop popupLoop;
            QTimer::singleShot(120, &popupLoop, &QEventLoop::quit);
            popupLoop.exec();
        }
        QPixmap snapshot = window.grab();
        if (openCombo) {
            if (auto *combo = window.findChild<QComboBox *>(QStringLiteral("rowLimitCombo"))) {
                QWidget *popup = combo->view()->window();
                QPainter painter(&snapshot);
                painter.drawPixmap(window.mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))),
                                   popup->grab());
            }
        }
        return snapshot.save(screenshotPath, "PNG") ? 0 : 2;
    }
    window.show();
    return app.exec();
}

#include "ui/IconProvider.h"

#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace vsdb {

QIcon databaseIcon(const QString &name)
{
    QSvgRenderer renderer(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    QIcon icon;
    for (const qreal dpr : {1.0, 2.0}) {
        const int pixels = qRound(16 * dpr);
        QPixmap pixmap(pixels, pixels);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(dpr);
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(0, 0, 16, 16));
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace vsdb

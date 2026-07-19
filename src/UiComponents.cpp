#include "UiComponents.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QIcon>
#include <QListView>
#include <QPalette>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QStyledItemDelegate>

namespace vsdb {

namespace {
class ComboItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(34);
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const QRect background = option.rect.adjusted(2, 1, -2, -1);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? QColor(QStringLiteral("#0868D7"))
                                   : hovered ? QColor(QStringLiteral("#2D2F39"))
                                             : QColor(QStringLiteral("#242630")));
        painter->drawRoundedRect(background, 5, 5);
        int textLeft = option.rect.left() + 11;
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QRect iconRect(option.rect.left() + 9, option.rect.center().y() - 8, 16, 16);
            icon.paint(painter, iconRect, Qt::AlignCenter,
                       selected ? QIcon::Selected : QIcon::Normal);
            textLeft = iconRect.right() + 8;
        }
        painter->setPen(QColor(QStringLiteral("#E7E8ED")));
        painter->drawText(QRect(textLeft, option.rect.top(),
                                option.rect.right() - textLeft - 8, option.rect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

void applyPopupPalette(QWidget *widget)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#242630")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#242630")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#292B35")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#E7E8ED")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E7E8ED")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#0868D7")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#E7E8ED")));
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
}
}

ModernComboBox::ModernComboBox(QWidget *parent) : QComboBox(parent)
{
    auto *list = new QListView(this);
    list->setObjectName(QStringLiteral("comboPopupView"));
    list->setUniformItemSizes(true);
    list->setMouseTracking(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setItemDelegate(new ComboItemDelegate(list));
    setView(list);
    setMaxVisibleItems(10);
    applyPopupPalette(this);
    applyPopupPalette(list);
    applyPopupPalette(list->viewport());
}

void ModernComboBox::showPopup()
{
    QComboBox::showPopup();

    QAbstractItemView *popupView = view();
    QWidget *popup = popupView ? popupView->window() : nullptr;
    if (!popup)
        return;

    applyPopupPalette(popupView);
    applyPopupPalette(popupView->viewport());
    applyPopupPalette(popup);
    popup->setObjectName(QStringLiteral("comboPopup"));
    popup->style()->unpolish(popup);
    popup->style()->polish(popup);

    const QRect available = screen() ? screen()->availableGeometry()
                                     : QApplication::primaryScreen()->availableGeometry();
    const int popupWidth = qMax(width(), popup->width());
    const QPoint below = mapToGlobal(QPoint(0, height() + 4));
    int popupY = below.y();
    if (popupY + popup->height() > available.bottom())
        popupY = mapToGlobal(QPoint(0, -popup->height() - 4)).y();

    const int popupX = qBound(available.left(), below.x(),
                              available.right() - popupWidth + 1);
    popup->setGeometry(popupX, popupY, popupWidth, popup->height());
}

} // namespace vsdb

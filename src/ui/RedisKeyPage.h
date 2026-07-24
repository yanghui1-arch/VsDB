#pragma once

#include "database/redis/RedisSession.h"

#include <QByteArray>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStandardItemModel;
class QTableView;

namespace vsdb {

class RedisKeyPage final : public QWidget
{
    Q_OBJECT

public:
    RedisKeyPage(RedisSession *session, QByteArray key,
                 QWidget *parent = nullptr);

    const QByteArray &key() const;
    void refresh();

signals:
    void keyRenamed(const QByteArray &oldKey, const QByteArray &newKey);
    void keyDeleted(const QByteArray &key);
    void keyMutated();

private:
    void applyDetails(RedisKeyDetails details);
    void saveString();
    void addEntry();
    void updateSelectedEntry();
    void deleteSelectedEntry();
    void renameKey();
    void deleteKey();
    QByteArray selectedIdentity() const;
    void showError(const QString &title, const QString &error);

    RedisSession *session_ = nullptr;
    QByteArray key_;
    RedisKeyDetails details_;
    QLabel *keyLabel_ = nullptr;
    QLabel *typeLabel_ = nullptr;
    QLabel *ttlLabel_ = nullptr;
    QLabel *sizeLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPlainTextEdit *stringEditor_ = nullptr;
    QTableView *table_ = nullptr;
    QStandardItemModel *model_ = nullptr;
    QPushButton *saveStringButton_ = nullptr;
    QPushButton *addButton_ = nullptr;
    QPushButton *updateButton_ = nullptr;
    QPushButton *deleteEntryButton_ = nullptr;
};

} // namespace vsdb

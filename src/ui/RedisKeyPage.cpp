#include "ui/RedisKeyPage.h"

#include "database/redis/RedisProtocol.h"
#include "ui/IconProvider.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace vsdb {

namespace {

QString ttlText(qint64 ttlMilliseconds)
{
    if (ttlMilliseconds == -1)
        return QStringLiteral("永久");
    if (ttlMilliseconds == -2)
        return QStringLiteral("键不存在");
    if (ttlMilliseconds < 1000)
        return QStringLiteral("%1 ms").arg(ttlMilliseconds);
    return QStringLiteral("%1 秒").arg(
        QString::number(ttlMilliseconds / 1000.0, 'f', 2));
}

QStandardItem *readOnlyItem(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    return item;
}

} // namespace

RedisKeyPage::RedisKeyPage(RedisSession *session, QByteArray key,
                           QWidget *parent)
    : QWidget(parent), session_(session), key_(std::move(key))
{
    setObjectName(QStringLiteral("redisKeyPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    auto *heading = new QHBoxLayout;
    keyLabel_ = new QLabel(this);
    keyLabel_->setObjectName(QStringLiteral("paneHeading"));
    keyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heading->addWidget(keyLabel_, 1);

    auto *refreshButton = new QPushButton(
        databaseIcon(QStringLiteral("refresh-cw")),
        QStringLiteral("刷新"), this);
    connect(refreshButton, &QPushButton::clicked,
            this, &RedisKeyPage::refresh);
    heading->addWidget(refreshButton);

    auto *renameButton = new QPushButton(
        databaseIcon(QStringLiteral("file-code")),
        QStringLiteral("重命名"), this);
    connect(renameButton, &QPushButton::clicked,
            this, &RedisKeyPage::renameKey);
    heading->addWidget(renameButton);

    auto *deleteButton = new QPushButton(
        databaseIcon(QStringLiteral("x")),
        QStringLiteral("删除键"), this);
    connect(deleteButton, &QPushButton::clicked,
            this, &RedisKeyPage::deleteKey);
    heading->addWidget(deleteButton);
    root->addLayout(heading);

    auto *metadata = new QWidget(this);
    auto *metadataLayout = new QFormLayout(metadata);
    metadataLayout->setContentsMargins(0, 0, 0, 0);
    metadataLayout->setHorizontalSpacing(28);
    typeLabel_ = new QLabel(metadata);
    ttlLabel_ = new QLabel(metadata);
    sizeLabel_ = new QLabel(metadata);
    metadataLayout->addRow(QStringLiteral("类型"), typeLabel_);
    metadataLayout->addRow(QStringLiteral("TTL"), ttlLabel_);
    metadataLayout->addRow(QStringLiteral("元素/字节数"), sizeLabel_);
    root->addWidget(metadata);

    stringEditor_ = new QPlainTextEdit(this);
    stringEditor_->setObjectName(QStringLiteral("redisValueEditor"));
    stringEditor_->setPlaceholderText(QStringLiteral("Redis String 值"));
    root->addWidget(stringEditor_, 1);

    saveStringButton_ = new QPushButton(
        databaseIcon(QStringLiteral("save")),
        QStringLiteral("保存 String（保留 TTL）"), this);
    saveStringButton_->setObjectName(QStringLiteral("primaryButton"));
    connect(saveStringButton_, &QPushButton::clicked,
            this, &RedisKeyPage::saveString);
    root->addWidget(saveStringButton_, 0, Qt::AlignRight);

    model_ = new QStandardItemModel(0, 2, this);
    table_ = new QTableView(this);
    table_->setModel(model_);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    root->addWidget(table_, 1);

    auto *entryActions = new QHBoxLayout;
    addButton_ = new QPushButton(
        databaseIcon(QStringLiteral("file-plus")),
        QStringLiteral("新增条目"), this);
    connect(addButton_, &QPushButton::clicked,
            this, &RedisKeyPage::addEntry);
    entryActions->addWidget(addButton_);
    updateButton_ = new QPushButton(
        databaseIcon(QStringLiteral("save")),
        QStringLiteral("修改选中项"), this);
    connect(updateButton_, &QPushButton::clicked,
            this, &RedisKeyPage::updateSelectedEntry);
    entryActions->addWidget(updateButton_);
    deleteEntryButton_ = new QPushButton(
        databaseIcon(QStringLiteral("x")),
        QStringLiteral("删除选中项"), this);
    connect(deleteEntryButton_, &QPushButton::clicked,
            this, &RedisKeyPage::deleteSelectedEntry);
    entryActions->addWidget(deleteEntryButton_);
    entryActions->addStretch();
    root->addLayout(entryActions);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("muted"));
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);
    refresh();
}

const QByteArray &RedisKeyPage::key() const
{
    return key_;
}

void RedisKeyPage::refresh()
{
    if (!session_ || !session_->isConnected()) {
        statusLabel_->setText(QStringLiteral("Redis 已断开，请重新连接后刷新。"));
        return;
    }
    QString error;
    RedisKeyDetails details = session_->inspectKey(
        key_, DefaultRedisPreviewItems,
        DefaultRedisValuePreviewBytes, &error);
    if (!error.isEmpty()) {
        showError(QStringLiteral("读取 Redis 键失败"), error);
        return;
    }
    applyDetails(std::move(details));
}

void RedisKeyPage::applyDetails(RedisKeyDetails details)
{
    details_ = std::move(details);
    keyLabel_->setText(redisDisplayBytes(key_, 240));
    typeLabel_->setText(QString::fromLatin1(details_.type));
    ttlLabel_->setText(ttlText(details_.ttlMilliseconds));
    sizeLabel_->setText(QString::number(details_.size));
    model_->removeRows(0, model_->rowCount());

    const bool isString = details_.type == QByteArrayLiteral("string");
    stringEditor_->setVisible(isString);
    saveStringButton_->setVisible(isString);
    table_->setVisible(!isString);
    addButton_->setVisible(!isString);
    updateButton_->setVisible(!isString);
    deleteEntryButton_->setVisible(!isString);

    if (isString) {
        const bool text = redisBytesAreText(details_.stringValue);
        stringEditor_->setPlainText(
            text ? QString::fromUtf8(details_.stringValue)
                 : redisDisplayBytes(details_.stringValue, -1));
        stringEditor_->setReadOnly(!text || details_.truncated);
        saveStringButton_->setEnabled(text && !details_.truncated);
        statusLabel_->setText(
            !text
                ? QStringLiteral("二进制值以十六进制只读显示，避免错误转码破坏数据。")
                : details_.truncated
                    ? QStringLiteral("值超过 1 MiB，仅显示安全预览；为避免截断覆盖，已禁用保存。")
                    : QStringLiteral("完整 String 值已加载。"));
        return;
    }

    if (details_.type == QByteArrayLiteral("list"))
        model_->setHorizontalHeaderLabels(
            {QStringLiteral("索引"), QStringLiteral("值")});
    else if (details_.type == QByteArrayLiteral("hash"))
        model_->setHorizontalHeaderLabels(
            {QStringLiteral("字段"), QStringLiteral("值")});
    else if (details_.type == QByteArrayLiteral("set"))
        model_->setHorizontalHeaderLabels(
            {QStringLiteral("成员"), QStringLiteral("值")});
    else if (details_.type == QByteArrayLiteral("zset"))
        model_->setHorizontalHeaderLabels(
            {QStringLiteral("成员"), QStringLiteral("分数")});
    else
        model_->setHorizontalHeaderLabels(
            {QStringLiteral("消息 / 字段"), QStringLiteral("值")});

    for (const RedisEntry &entry : details_.entries) {
        auto *label = readOnlyItem(redisDisplayBytes(entry.label));
        label->setData(entry.identity, Qt::UserRole);
        model_->appendRow(
            {label, readOnlyItem(redisDisplayBytes(entry.value))});
    }

    const bool canUpdate = details_.type == QByteArrayLiteral("hash")
        || details_.type == QByteArrayLiteral("list")
        || details_.type == QByteArrayLiteral("zset");
    const bool canDelete = details_.type == QByteArrayLiteral("hash")
        || details_.type == QByteArrayLiteral("set")
        || details_.type == QByteArrayLiteral("zset")
        || details_.type == QByteArrayLiteral("stream");
    updateButton_->setEnabled(canUpdate);
    deleteEntryButton_->setEnabled(canDelete);
    statusLabel_->setText(
        details_.truncated
            ? QStringLiteral("为控制内存占用，仅显示前 %1 项；总数 %2。")
                  .arg(details_.entries.size()).arg(details_.size)
            : QStringLiteral("已准确加载 %1 项。").arg(details_.entries.size()));
}

void RedisKeyPage::saveString()
{
    QString error;
    if (!session_->updateString(key_, stringEditor_->toPlainText().toUtf8(),
                                &error)) {
        showError(QStringLiteral("保存 Redis String 失败"), error);
        return;
    }
    emit keyMutated();
    refresh();
}

void RedisKeyPage::addEntry()
{
    bool accepted = false;
    QByteArray identity;
    QByteArray value;
    if (details_.type == QByteArrayLiteral("list")) {
        const QString listValue = QInputDialog::getMultiLineText(
            this, QStringLiteral("追加 List 元素"), QStringLiteral("值"),
            {}, &accepted);
        if (!accepted)
            return;
        identity = listValue.toUtf8();
    } else {
        const QString label =
            details_.type == QByteArrayLiteral("hash")
                ? QStringLiteral("字段")
                : details_.type == QByteArrayLiteral("stream")
                    ? QStringLiteral("字段")
                    : QStringLiteral("成员");
        const QString identityText = QInputDialog::getText(
            this, QStringLiteral("新增 Redis 条目"), label,
            QLineEdit::Normal, {}, &accepted);
        if (!accepted || identityText.isEmpty())
            return;
        identity = identityText.toUtf8();
        if (details_.type == QByteArrayLiteral("hash")
            || details_.type == QByteArrayLiteral("stream")) {
            const QString entryValue = QInputDialog::getMultiLineText(
                this, QStringLiteral("新增 Redis 条目"),
                QStringLiteral("值"), {}, &accepted);
            if (!accepted)
                return;
            value = entryValue.toUtf8();
        } else if (details_.type == QByteArrayLiteral("zset")) {
            const double score = QInputDialog::getDouble(
                this, QStringLiteral("新增 Sorted Set 成员"),
                QStringLiteral("分数"), 0.0, -1e100, 1e100, 6, &accepted);
            if (!accepted)
                return;
            value = QByteArray::number(score, 'g', 16);
        }
    }

    QString error;
    if (!session_->addEntry(details_.type, key_, identity, value, &error)) {
        showError(QStringLiteral("新增 Redis 条目失败"), error);
        return;
    }
    emit keyMutated();
    refresh();
}

void RedisKeyPage::updateSelectedEntry()
{
    const QByteArray identity = selectedIdentity();
    if (identity.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择条目"),
                                 QStringLiteral("请先选择要修改的 Redis 条目。"));
        return;
    }
    bool accepted = false;
    QByteArray value;
    if (details_.type == QByteArrayLiteral("zset")) {
        const double score = QInputDialog::getDouble(
            this, QStringLiteral("修改 Sorted Set 分数"),
            QStringLiteral("分数"), 0.0, -1e100, 1e100, 6, &accepted);
        if (!accepted)
            return;
        value = QByteArray::number(score, 'g', 16);
    } else {
        const QString entryValue = QInputDialog::getMultiLineText(
            this, QStringLiteral("修改 Redis 条目"),
            QStringLiteral("新值"), {}, &accepted);
        if (!accepted)
            return;
        value = entryValue.toUtf8();
    }
    QString error;
    if (!session_->updateEntry(details_.type, key_, identity, value, &error)) {
        showError(QStringLiteral("修改 Redis 条目失败"), error);
        return;
    }
    emit keyMutated();
    refresh();
}

void RedisKeyPage::deleteSelectedEntry()
{
    const QByteArray identity = selectedIdentity();
    if (identity.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择条目"),
                                 QStringLiteral("请先选择要删除的 Redis 条目。"));
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("删除 Redis 条目"),
            QStringLiteral("确定删除选中的条目？此操作无法撤销。"))
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!session_->deleteEntry(details_.type, key_, identity, &error)) {
        showError(QStringLiteral("删除 Redis 条目失败"), error);
        return;
    }
    emit keyMutated();
    refresh();
}

void RedisKeyPage::renameKey()
{
    bool accepted = false;
    const QString renamed = QInputDialog::getText(
        this, QStringLiteral("重命名 Redis 键"),
        QStringLiteral("新键名"), QLineEdit::Normal,
        redisBytesAreText(key_) ? QString::fromUtf8(key_) : QString{},
        &accepted);
    if (!accepted || renamed.isEmpty())
        return;
    const QByteArray newKey = renamed.toUtf8();
    QString error;
    if (!session_->renameKey(key_, newKey, &error)) {
        showError(QStringLiteral("重命名 Redis 键失败"), error);
        return;
    }
    const QByteArray oldKey = key_;
    key_ = newKey;
    emit keyRenamed(oldKey, newKey);
    refresh();
}

void RedisKeyPage::deleteKey()
{
    if (QMessageBox::question(
            this, QStringLiteral("删除 Redis 键"),
            QStringLiteral("确定删除 %1？此操作无法撤销。")
                .arg(redisDisplayBytes(key_, 180)))
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!session_->deleteKey(key_, &error)) {
        showError(QStringLiteral("删除 Redis 键失败"), error);
        return;
    }
    emit keyDeleted(key_);
}

QByteArray RedisKeyPage::selectedIdentity() const
{
    if (!table_->currentIndex().isValid())
        return {};
    return model_->item(table_->currentIndex().row(), 0)
        ->data(Qt::UserRole).toByteArray();
}

void RedisKeyPage::showError(const QString &title, const QString &error)
{
    statusLabel_->setText(error);
    QMessageBox::critical(
        this, title,
        error.isEmpty() ? QStringLiteral("未知 Redis 错误") : error);
}

} // namespace vsdb

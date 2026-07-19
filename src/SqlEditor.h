#pragma once

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace vsdb {

class LineNumberArea;

class SqlEditor final : public QPlainTextEdit
{
public:
    explicit SqlEditor(QWidget *parent = nullptr);
    int lineNumberAreaWidth() const;
    void paintLineNumberArea(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

private:
    LineNumberArea *lineNumberArea_;
};

class SqlHighlighter final : public QSyntaxHighlighter
{
public:
    explicit SqlHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule { QRegularExpression pattern; QTextCharFormat format; };
    QList<Rule> rules_;
};

} // namespace vsdb

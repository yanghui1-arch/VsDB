#include "SqlEditor.h"

#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>

namespace vsdb {

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(SqlEditor *editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return QSize(editor_->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { editor_->paintLineNumberArea(event); }

private:
    SqlEditor *editor_;
};

SqlEditor::SqlEditor(QWidget *parent)
    : QPlainTextEdit(parent), lineNumberArea_(new LineNumberArea(this))
{
    setObjectName(QStringLiteral("sqlEditor"));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * 4.0);
    QFont codeFont(QStringLiteral("Cascadia Code"));
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setPointSize(11);
    setFont(codeFont);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &SqlEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &SqlEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &SqlEditor::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
    new SqlHighlighter(document());
}

int SqlEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int count = qMax(1, blockCount()); count >= 10; count /= 10)
        ++digits;
    return 17 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void SqlEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void SqlEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea_->scroll(0, dy);
    else
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void SqlEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    lineNumberArea_->setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void SqlEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(QStringLiteral("#242630")));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
}

void SqlEditor::paintLineNumberArea(QPaintEvent *event)
{
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), QColor(QStringLiteral("#1C1D23")));
    painter.setPen(QColor(QStringLiteral("#747885")));

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top())
            painter.drawText(0, top, lineNumberArea_->width() - 9, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

SqlHighlighter::SqlHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    QTextCharFormat keyword;
    keyword.setForeground(QColor(QStringLiteral("#FC5FA3")));
    keyword.setFontWeight(QFont::DemiBold);
    rules_.append({QRegularExpression(
        QStringLiteral("\\b(SELECT|FROM|WHERE|LEFT|RIGHT|INNER|OUTER|JOIN|ON|AS|AND|OR|ORDER|BY|DESC|ASC|LIMIT|GROUP|HAVING|INSERT|UPDATE|DELETE|INTO|VALUES|CREATE|TABLE|NULL|IS|NOT)\\b"),
        QRegularExpression::CaseInsensitiveOption), keyword});

    QTextCharFormat schema;
    schema.setForeground(QColor(QStringLiteral("#D6D7DD")));
    rules_.append({QRegularExpression(QStringLiteral("\\b(public|analytics)\\.[A-Za-z_][A-Za-z0-9_]*\\b")), schema});

    QTextCharFormat string;
    string.setForeground(QColor(QStringLiteral("#FC6A5D")));
    rules_.append({QRegularExpression(QStringLiteral("'[^']*'")), string});

    QTextCharFormat number;
    number.setForeground(QColor(QStringLiteral("#D0BF69")));
    rules_.append({QRegularExpression(QStringLiteral("\\b[0-9]+(?:\\.[0-9]+)?\\b")), number});

    QTextCharFormat comment;
    comment.setForeground(QColor(QStringLiteral("#707582")));
    comment.setFontItalic(true);
    rules_.append({QRegularExpression(QStringLiteral("--[^\\n]*")), comment});
}

void SqlHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : rules_) {
        auto match = rule.pattern.globalMatch(text);
        while (match.hasNext()) {
            const auto current = match.next();
            setFormat(current.capturedStart(), current.capturedLength(), rule.format);
        }
    }
}

} // namespace vsdb

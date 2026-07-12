#pragma once

#include <QKeySequence>
#include <QObject>

class QAction;

namespace vsdb {

class CommandRegistry final : public QObject {
    Q_OBJECT
public:
    explicit CommandRegistry(QObject *parent = nullptr);
    QAction *newQuery() const;
    QAction *runQuery() const;
    QAction *stopQuery() const;
    QAction *refresh() const;
    QAction *clearEditor() const;
    QAction *closeQuery() const;
    QAction *toggleExplorer() const;
    QAction *toggleInspector() const;
    QAction *toggleStatusBar() const;
    QAction *about() const;

private:
    QAction *makeAction(const QString &text, const QString &iconPath, const QKeySequence &shortcut = {});
    QAction *newQuery_;
    QAction *runQuery_;
    QAction *stopQuery_;
    QAction *refresh_;
    QAction *clearEditor_;
    QAction *closeQuery_;
    QAction *toggleExplorer_;
    QAction *toggleInspector_;
    QAction *toggleStatusBar_;
    QAction *about_;
};

} // namespace vsdb

#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QSplitter;

namespace vsdb {
class CommandRegistry;
class ConnectionExplorer;
class InspectorPane;
class QueryWorkspace;
class WorkspaceController;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, bool restoreSettings = true);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createToolBar();
    void connectCommands();
    void restoreWindowSettings();
    void saveWindowSettings();
    void setPaneVisible(QWidget *pane, bool visible, int preferredWidth);
    void executeCurrentQuery();

    bool restoreSettings_;
    WorkspaceController *controller_;
    CommandRegistry *commands_;
    ConnectionExplorer *explorer_;
    QueryWorkspace *workspace_;
    InspectorPane *inspector_;
    QLineEdit *searchBox_;
    QSplitter *mainSplitter_;
    QLabel *statusState_;
    QLabel *statusMetrics_;
};

} // namespace vsdb

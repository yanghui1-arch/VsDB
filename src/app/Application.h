#pragma once

#include <QApplication>

namespace vsdb {

class Application final : public QApplication {
public:
    Application(int &argc, char **argv);

private:
    void applyStyleSheet();
};

} // namespace vsdb

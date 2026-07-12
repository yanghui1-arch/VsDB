#include "app/Application.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    vsdb::Application application(argc, argv);
    vsdb::MainWindow window;
    window.show();
    return application.exec();
}

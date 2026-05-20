#include "main_window.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/resources/app_icon.ico")));
    MainWindow w;
    w.show();
    return QCoreApplication::exec();
}

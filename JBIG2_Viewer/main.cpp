#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("JBIG2_image_viewer");
    QCoreApplication::setApplicationVersion("1.0");

    MainWindow w;
    w.show();
    return a.exec();
}

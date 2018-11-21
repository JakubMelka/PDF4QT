#include "pdfviewermainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    pdfviewer::PDFViewerMainWindow w;
    w.show();

    return a.exec();
}

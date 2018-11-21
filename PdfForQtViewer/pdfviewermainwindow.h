#ifndef PDFVIEWERMAINWINDOW_H
#define PDFVIEWERMAINWINDOW_H

#include <QMainWindow>

namespace Ui
{
class PDFViewerMainWindow;
}

namespace pdfviewer
{

class PDFViewerMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PDFViewerMainWindow(QWidget *parent = nullptr);
    virtual ~PDFViewerMainWindow() override;

private:
    void onActionOpenTriggered();

    Ui::PDFViewerMainWindow* ui;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H

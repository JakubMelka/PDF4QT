#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "pdfexception.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public pdf::PDFRenderErrorReporter
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    virtual ~MainWindow() override;

    virtual void reportRenderError(pdf::RenderErrorType type, QString message) override;
    virtual void reportRenderErrorOnce(pdf::RenderErrorType type, QString message) override;

private slots:
    void on_actionAddImage_triggered();
    void on_actionClear_triggered();
    void on_actionAdd_JBIG2_image_triggered();

private:
    void addImage(QString title, QImage image);

    Ui::MainWindow* ui;
    QString m_directory;
};
#endif // MAINWINDOW_H

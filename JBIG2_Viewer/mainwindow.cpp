#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "pdfexception.h"
#include "pdfjbig2decoder.h"

#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSettings>
#include <QMessageBox>
#include <QElapsedTimer>

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowState(Qt::WindowMaximized);

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    m_directory = settings.value("Directory").toString();
}

MainWindow::~MainWindow()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("Directory", m_directory);

    delete ui;
}

void MainWindow::reportRenderError(pdf::RenderErrorType type, QString message)
{
    Q_UNUSED(type);
    QMessageBox::critical(this, tr("Error"), message);
}

void MainWindow::on_actionAddImage_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open image"), m_directory, QString("Bitmap image (*.bmp)"));
    if (QFile::exists(fileName))
    {
        QImageReader reader(fileName);
        QImage image = reader.read();

        if (!image.isNull())
        {
            QFileInfo file(fileName);
            m_directory = file.filePath();
            addImage(file.fileName(), qMove(image));
        }
    }
}

void MainWindow::addImage(QString title, QImage image)
{
    QGroupBox* groupBox = new QGroupBox(this);
    QHBoxLayout* layout = new QHBoxLayout(groupBox);
    groupBox->setTitle(title);
    QLabel* label = new QLabel(groupBox);
    layout->addWidget(label);
    label->setPixmap(QPixmap::fromImage(image));
    label->setFixedSize(image.size());
    ui->imagesMainLayout->addWidget(groupBox);
}

void MainWindow::on_actionClear_triggered()
{
    for (int i = 0; i < ui->imagesMainLayout->count(); ++i)
    {
        ui->imagesMainLayout->itemAt(i)->widget()->deleteLater();
    }
}

void MainWindow::on_actionAdd_JBIG2_image_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open image"), m_directory, QString("JBIG2 image (*.jb2)"));
    if (QFile::exists(fileName))
    {
        QFile file(fileName);
        if (file.open(QFile::ReadOnly))
        {
            m_directory = QFileInfo(file).filePath();
            QByteArray data = file.readAll();
            file.close();

            try
            {
                pdf::PDFJBIG2Decoder decoder(data, QByteArray(), this);

                QElapsedTimer timer;
                timer.start();
                pdf::PDFImageData imageData = decoder.decodeFileStream();
                qint64 time = timer.elapsed();

                if (imageData.isValid())
                {
                    QImage image(imageData.getWidth(), imageData.getHeight(), QImage::Format_Mono);
                    const uchar* sourceData = reinterpret_cast<const uchar*>(imageData.getData().constData());
                    Q_ASSERT(imageData.getData().size() == image.byteCount());
                    std::transform(sourceData, sourceData + imageData.getData().size(), image.bits(), [](const uchar value) { return value; });
                    addImage(file.fileName() + QString(", Decoded in %1 [msec]").arg(time), qMove(image));
                }
            }
            catch (pdf::PDFException exception)
            {
                QMessageBox::critical(this, tr("Error"), exception.getMessage());
            }
        }
    }
}

void MainWindow::reportRenderErrorOnce(pdf::RenderErrorType type, QString message)
{
    Q_UNUSED(type);
    QMessageBox::critical(this, tr("Error"), message);
}

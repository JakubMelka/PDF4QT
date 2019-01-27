//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfviewermainwindow.h"
#include "ui_pdfviewermainwindow.h"

#include "pdfdocumentreader.h"
#include "pdfvisitor.h"
#include "pdfstreamfilters.h"
#include "pdfdrawwidget.h"

#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QDesktopWidget>
#include <QStandardPaths>

namespace pdfviewer
{

PDFViewerMainWindow::PDFViewerMainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::PDFViewerMainWindow),
    m_pdfWidget(nullptr)
{
    ui->setupUi(this);

    // Initialize shortcuts
    ui->actionOpen->setShortcut(QKeySequence::Open);
    ui->actionClose->setShortcut(QKeySequence::Close);
    ui->actionQuit->setShortcut(QKeySequence::Quit);

    connect(ui->actionOpen, &QAction::triggered, this, &PDFViewerMainWindow::onActionOpenTriggered);
    connect(ui->actionClose, &QAction::triggered, this, &PDFViewerMainWindow::onActionCloseTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &PDFViewerMainWindow::onActionQuitTriggered);

    m_pdfWidget = new pdf::PDFWidget(this);
    setCentralWidget(m_pdfWidget);
    setFocusProxy(m_pdfWidget);

    readSettings();
}

PDFViewerMainWindow::~PDFViewerMainWindow()
{
    delete ui;
}

void PDFViewerMainWindow::onActionOpenTriggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select PDF document"), m_directory, tr("PDF document (*.pdf)"));
    if (!fileName.isEmpty())
    {
        openDocument(fileName);
    }
}

void PDFViewerMainWindow::onActionCloseTriggered()
{
    closeDocument();
}

void PDFViewerMainWindow::onActionQuitTriggered()
{
    close();
}

void PDFViewerMainWindow::readSettings()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty())
    {
        QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
        QRect windowRect(0, 0, availableGeometry.width() / 2, availableGeometry.height() / 2);
        windowRect = windowRect.translated(availableGeometry.center() - windowRect.center());
        setGeometry(windowRect);
    }
    else
    {
        restoreGeometry(geometry);
    }

    QByteArray state = settings.value("windowState", QByteArray()).toByteArray();
    if (!state.isEmpty())
    {
        restoreState(state);
    }

    m_directory = settings.value("defaultDirectory", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
}

void PDFViewerMainWindow::writeSettings()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("defaultDirectory", m_directory);
}

void PDFViewerMainWindow::updateTitle()
{
    if (m_pdfDocument)
    {
        QString title = m_pdfDocument->getInfo()->title;
        if (title.isEmpty())
        {
            title = m_currentFile;
        }
        setWindowTitle(tr("%1 - PDF Viewer").arg(m_currentFile));
    }
    else
    {
        setWindowTitle(tr("PDF Viewer"));
    }
}

void PDFViewerMainWindow::openDocument(const QString& fileName)
{
    // First close old document
    closeDocument();

    // Try to open a new document
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pdf::PDFDocumentReader reader;
    pdf::PDFDocument document = reader.readFromFile(fileName);
    QApplication::restoreOverrideCursor();

    if (reader.isSuccessfull())
    {
        // Mark current directory as this
        QFileInfo fileInfo(fileName);
        m_directory = fileInfo.dir().absolutePath();
        m_currentFile = fileInfo.fileName();

        m_pdfDocument.reset(new pdf::PDFDocument(std::move(document)));
        setDocument(m_pdfDocument.data());

        statusBar()->showMessage(tr("Document '%1' was successfully loaded!").arg(fileName), 4000);
    }
    else
    {
        QMessageBox::critical(this, tr("PDF Viewer"), tr("Document read error: %1").arg(reader.getErrorMessage()));
    }
}

void PDFViewerMainWindow::setDocument(const pdf::PDFDocument* document)
{
    m_pdfWidget->setDocument(document);
    updateTitle();
}

void PDFViewerMainWindow::closeDocument()
{
    setDocument(nullptr);
    m_pdfDocument.reset();
}

void PDFViewerMainWindow::closeEvent(QCloseEvent* event)
{
    writeSettings();
    closeDocument();
    event->accept();
}

}   // namespace pdfviewer
